#!/usr/bin/env python3

from colorama import Fore, Back, Style
from tqdm import tqdm
from yattag import Doc, indent
import altair
import argparse
import datetime
import glob
import numpy
import os
import pandas
import re
import subprocess
import time
import yamale
import yaml


MUTABLE_BINARY  = os.path.join(os.getcwd(), 'build', 'release', 'bin', 'shell')
YML_SCHEMA      = os.path.join(os.getcwd(), 'benchmark', '_schema.yml')


class BenchmarkError(Exception):
    pass

#-----------------------------------------------------------------------------------------------------------------------
# Helper functions
#-----------------------------------------------------------------------------------------------------------------------

in_red   = lambda x: f'{Fore.RED}{x}{Style.RESET_ALL}'
in_green = lambda x: f'{Fore.GREEN}{x}{Style.RESET_ALL}'
in_bold  = lambda x: f'{Style.BRIGHT}{x}{Style.RESET_ALL}'

def validate_schema(path_to_file, path_to_schema) -> bool:
    schema = yamale.make_schema(path_to_schema)
    data = yamale.make_data(path_to_file)
    try:
        yamale.validate(schema, data)
    except ValueError:
        return False
    return True

def time_command(command, query, pattern, num_runs=3):
    cmd = command + [ '--quiet', '-' ]
    input = '\n'.join([query] * num_runs) + '\n'

    process = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                               cwd=os.getcwd())

    TIMEOUT = 60
    try:
        out, err = process.communicate(input.encode('latin-1'), timeout=TIMEOUT)
    except subprocess.TimeoutExpired:
        process.kill()
        raise BenchmarkError(f'Benchmark timed out after {TIMEOUT} seconds')

    out = out.decode('latin-1')
    err = err.decode('latin-1')

    if process.returncode or len(err):
        query = query.encode('unicode_escape').decode()
        tqdm.write(f'''\
Unexpected failure during execution of benchmark "{path_to_benchmark}" with return code {process.returncode}:
$ echo -e "{query}" | {' '.join(cmd)}
===== stdout =====
{out}
===== stderr =====
{err}
==================
''')
        raise BenchmarkError(f'Benchmark failed with return code {process.returncode}.')

    # Parse `out` for timings
    durations = list()
    matcher = re.compile(pattern)
    for line in out.split('\n'):
        if matcher.match(line):
            for s in line.split():
                try:
                    dur = float(s)
                    durations.append(dur)
                except ValueError:
                    continue
    return durations

def perform_benchmark(path_to_benchmark):
    # Get benchmark schema
    schema = os.path.join(os.path.dirname(path_to_benchmark), 'data', 'schema.sql')

    # Open file
    with open(path_to_benchmark, 'r') as f:
        yml = yaml.safe_load(f)

    suite = yml['suite']
    benchmark = yml['benchmark']
    name = os.path.splitext(os.path.basename(path_to_benchmark))[0]

    # Collect results in data frame
    measurements = pandas.DataFrame(columns=['date', 'suite', 'benchmark', 'name', 'case', 'time'])

    command = [ MUTABLE_BINARY, '-t', schema ]
    if args.binargs:
        command.extend(args.binargs.split(' '))
    for case, query in yml['cases'].items():
        query = query.strip().replace('\n', ' ')
        if args.verbose:
            cmd_str = ' '.join(command)
            tqdm.write(f'$ echo -e "{query}" | {cmd_str} -')
        try:
            durations = time_command(command, query, yml['pattern'], 5)
        except BenchmarkError as ex:
            tqdm.write(f'Benchmark {suite}/{benchmark} failed: {str(ex)}')
            continue

        for dur in durations:
            #  csv.write(f'{date},{suite},{benchmark},{name},{case},{dur}\n')
            measurements.loc[len(measurements)] = [ date, suite, benchmark, name, case, dur ]

    return suite, benchmark, name, measurements, yml


#-----------------------------------------------------------------------------------------------------------------------
# main
#-----------------------------------------------------------------------------------------------------------------------

if __name__ == '__main__':
    # Parse args
    parser = argparse.ArgumentParser(description='''Run benchmarks on mutable.
                                                    The build directory is assumed to be ./build/release .''')
    parser.add_argument('suite', nargs='*', help='a benchmark suite to be run')
    parser.add_argument('--args', help='provide additional arguments to pass through to the binary', dest='binargs',
                                  metavar='ARGS', default=None, action='store')
    parser.add_argument('-v', '--verbose', help='verbose output', dest='verbose', default=False, action='store_true')
    args = parser.parse_args()

    # Check whether we are interactive
    is_interactive = True if 'TERM' in os.environ else False

    # Get benchmark files
    if not args.suite:
        benchmark_files = sorted(glob.glob(os.path.join('benchmark', '**', '[!_]*.yml'), recursive=True))
    else:
        benchmark_files = []
        for suite in sorted(set(args.suite)):
            benchmark_files.extend(sorted(glob.glob(os.path.join('benchmark', suite, '**', '[!_]*.yml'), recursive=True)))

    # Set up counters
    num_benchmarks_total = len(benchmark_files)
    num_benchmarks_passed = 0

    # Set up event log
    log = tqdm(total=0, position=1, ncols=80, leave=False, bar_format='{desc}', disable=not is_interactive)

    # Get date
    date = datetime.date.today().isoformat()

    # Write measurements to CSV file
    with open('benchmark.csv', 'w') as csv:
        csv.write('date,suite,benchmark,name,case,time\n')

    benchmark_results = dict()

    for path_to_benchmark in tqdm(benchmark_files, position=0, ncols=80, leave=False,
                                  bar_format='|{bar}| {n}/{total}', disable=not is_interactive):
        # Log current file
        log.set_description_str(f'Running benchmark "{path_to_benchmark}"'.ljust(80))

        # Validate schema
        if not validate_schema(path_to_benchmark, YML_SCHEMA):
            tqdm.write(f'Benchmark file "{path_to_benchmark}" violates schema.')
            continue

        # Perform the benchmark
        suite, benchmark, name, measurements, yml = perform_benchmark(path_to_benchmark)
        num_benchmarks_passed += 1

        # Write measurements to CSV file
        measurements.to_csv('benchmark.csv', index=False, header=False, mode='a')

        # Add measurements to benchmark results dictionary
        the_suite = benchmark_results.get(suite, dict())
        the_benchmark = the_suite.get(benchmark, dict())
        the_benchmark[name] = (measurements, yml)
        the_suite[benchmark] = the_benchmark
        benchmark_results[suite] = the_suite

    doc, tag, text = Doc().tagtext()
    doc.asis('<!DOCTYPE html>')
    current_suite = None
    current_benchmark = None
    with tag('html'):
        with tag('head'):
            with tag('script', src='https://cdn.jsdelivr.net/npm/vega@5'):
                pass
            with tag('script', src='https://cdn.jsdelivr.net/npm/vega-lite@3'):
                pass
            with tag('script', src='https://cdn.jsdelivr.net/npm/vega-embed@4'):
                pass
            doc.stag('link', rel='stylesheet', type='text/css',
                             href='https://stackpath.bootstrapcdn.com/bootstrap/4.4.1/css/bootstrap.min.css',
                             integrity='sha384-Vkoo8x4CGsO3+Hhxv8T/Q5PaXtkKtu6ug5TOeNV6gBiFeWPGFN9MuhOf23Q9Ifjh',
                             crossorigin='anonymous')
            doc.stag('link', rel='stylesheet', type='text/css', href='benchmark/style.css')
            doc.stag('link', rel='stylesheet',
                             href='https://cdnjs.cloudflare.com/ajax/libs/font-awesome/5.11.2/css/all.min.css',
                             integrity='sha256-+N4/V/SbAFiW1MPBCXnfnP9QSN3+Keu+NlB+0ev/YKQ=',
                             crossorigin='anonymous')
        with tag('body'):
            # Generate top nav bar
            with tag('nav', klass='navbar navbar-expand-md navbar-dark bg-dark navbar-shadow sticky-top'):
                with tag('a', klass='navbar-brand', href='#'):
                    doc.asis('<i class="fas fa-cubes ico-rotate"></i> mu<i class="text-mutable">t</i>able')
                    # TODO link CSV file with measurements
                with tag('div', klass='collapse navbar-collapse'):
                    with tag('ul', klass='navbar-nav mr-auto'):
                        with tag('li', klass='nav-item active'):
                            doc.line('a', 'Benchmark Results', klass='nav-link', href='#')

            with tag('div', klass='container-fluid'):
                with tag('div', klass='row'):
                    # Generate nav sidebar
                    with tag('nav', klass='col-md-2 d-none d-md-block bg-light sidebar'):
                        with tag('div', klass='sidebar-sticky position-fixed'):
                            with tag('h6', klass='sidebar-heading px-3 mt-4 mb-1 text-muted'):
                                text('Benchmark Suites')
                            with tag('ul', klass='nav flex-column'):
                                for suite, benchmarks in benchmark_results.items():
                                    with tag('li', klass='nav-item'):
                                        with tag('a', klass='nav-link', href=f'#{suite}'):
                                            doc.line('i', '', klass='far fa-arrow-alt-circle-right')
                                            text(suite)
                                        with tag('ul', klass='nav flex-column'):
                                            for benchmark in benchmarks.keys():
                                                with tag('li', klass='nav-item'):
                                                    with tag('a', klass='nav-link', href=f'#{suite}_{benchmark}'):
                                                        text(benchmark)

                    # Generate main content
                    with tag('main', role='main', klass='col-md-9 ml-sm-auto col-lg-10 px-4'):
                        for suite, benchmarks in benchmark_results.items():
                            with tag('div', id=suite, klass='suite'):
                                doc.line('h2', suite)
                                for benchmark, names in benchmarks.items():
                                    with tag('div', id=f'{suite}_{benchmark}', klass='benchmark'):
                                        doc.line('h3', benchmark)
                                        for name, _ in names.items():
                                            with tag('div', id=f'chart_{suite}_{benchmark}_{name}'):
                                                pass
                                        doc.stag('div', id=f'chart_{suite}_{benchmark}')

            with tag('script', src='https://code.jquery.com/jquery-3.4.1.slim.min.js',
                               integrity='sha384-J6qa4849blE2+poT4WnyKhv5vZF5SrPo0iEjwBvKU7imGFAV0wwj1yYfoRSJoZ+n',
                               crossorigin='anonymous'):
                pass
            with tag('script', src='https://cdn.jsdelivr.net/npm/popper.js@1.16.0/dist/umd/popper.min.js',
                               integrity='sha384-Q6E9RHvbIyZFJoft+2mJbHaEWldlvI9IOYy5n3zV9zzTtmI3UksdQRVvoxMfooAo',
                               crossorigin='anonymous'):
                pass
            with tag('script', src='https://stackpath.bootstrapcdn.com/bootstrap/4.4.1/js/bootstrap.min.js',
                               integrity='sha384-wfSDF2E50Y2D1uUdj0O3uMBJnjuUD4Ih7YwaYd1iqfktj0Uod8GCExl3Og8ifwB6',
                               crossorigin='anonymous'):
                pass

            for suite, benchmarks in benchmark_results.items():
                for benchmark, names in benchmarks.items():
                    combined_measurements = None
                    for name, data in names.items():
                        measurements, yml = data
                        num_cases = len(measurements['case'].unique())
                        chart_width = 30 * num_cases
                        chart_title = yml['description']
                        base = altair.Chart(measurements, title=chart_title, width=chart_width).encode(
                            x = altair.X('case:N', title='Cases')
                        )
                        box = base.mark_boxplot().encode(
                            y = altair.Y('time:Q', title='Time (ms)')
                        )
                        line = base.mark_line(color='red').encode(y='mean(time)')
                        chart = box + line

                        combined_measurements = pandas.concat([combined_measurements, measurements],
                                                              ignore_index=True, sort=False)

                        with tag('script', type='text/javascript'):
                            text(f'var spec = {chart.to_json()};')
                            text('var opt = {"renderer": "canvas", "actions": false};')
                            text(f'vegaEmbed("#chart_{suite}_{benchmark}_{name}" , spec, opt);')

                    num_cases = len(combined_measurements['case'].unique())
                    chart_width = 50 * num_cases
                    chart_title = f'Combined chart for {suite} / {benchmark}.'
                    base = altair.Chart(combined_measurements, title=chart_title, width=chart_width).encode(
                        x = altair.X('case:N', title='Cases'),
                        color = 'name'
                    )
                    line = base.mark_line().encode(
                        y = altair.Y('mean(time)', title='Time (ms)')
                    )
                    band = base.mark_errorband(extent='ci').encode(
                        y = 'time'
                    )
                    chart = line + band
                    with tag('script', type='text/javascript'):
                        text(f'var spec = {chart.to_json()};')
                        text('var opt = {"renderer": "canvas", "actions": false};')
                        text(f'vegaEmbed("#chart_{suite}_{benchmark}" , spec, opt);')


    with open('benchmark.html', 'w') as html:
        html.write(indent(doc.getvalue()))
        html.write('\n')

    # Close event log
    log.clear()
    log.close()

    exit(num_benchmarks_passed != num_benchmarks_total)
