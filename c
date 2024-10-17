#!/usr/bin/env php
<?php

const ROOT = '/home/ilutov/Developer/php-src';

if (getcwd() !== ROOT) {
    fprintf(STDERR, "c must be run within the php-src root directory");
    exit(1);
}

const CORES = 16;

function runCommand(array $args, ?array $envVars = null, bool $hideStdout = true) {
    $cmd = '';
    if ($envVars === []) {
        $envVars = null;
    }
    if ($envVars) {
        foreach ($envVars as $envName => $envValue) {
            $cmd .= $envName . "='" . $envValue . "'";
            $cmd .= ' ';
        }
    }
    $cmd .= implode(' ', array_map('escapeshellarg', $args));
    if ($envVars) {
        $envVars = array_merge(getenv(), $envVars);
    }

    $pipes = null;
    $descriptorSpec = [0 => ['pipe', 'r'], 1 => ['pipe', 'w'], 2 => ['pipe', 'w']];
    fwrite(STDOUT, "> $cmd\n");
    $processHandle = proc_open($args, $descriptorSpec, $pipes, getcwd(), $envVars);

    $stdin = $pipes[0];
    $stdout = $pipes[1];
    $stderr = $pipes[2];

    fclose($stdin);

    stream_set_blocking($stdout, false);
    stream_set_blocking($stderr, false);

    $stdoutEof = false;
    $stderrEof = false;

    do {
        $read = [$stdout, $stderr];
        $write = null;
        $except = null;

        stream_select($read, $write, $except, 1, 0);

        foreach ($read as $stream) {
            $chunk = fgets($stream);
            if ($stream === $stdout) {
                if (!$hideStdout) {
                    fprintf(STDOUT, "%s", $chunk);
                }
            } elseif ($stream === $stderr) {
                fprintf(STDERR, "%s", $chunk);
            }
        }

        $stdoutEof = $stdoutEof || feof($stdout);
        $stderrEof = $stderrEof || feof($stderr);
    } while(!$stdoutEof || !$stderrEof);

    fclose($stdout);
    fclose($stderr);

    $statusCode = proc_close($processHandle);
    if ($statusCode !== 0) {
        exit($statusCode);
    }
}

function build() {
    runCommand(['make', '-j' . CORES]);
}

function rebuild($args) {
    $configureFlags = [
        '--with-config-file-path=' . realpath($_SERVER['HOME'] . '/.local/lib'),
        '--disable-phpdbg',
    ];
    $envVars = [];
    $debug = true;
    $asan = false;
    $ubsan = false;

    foreach ($args as $arg) {
        switch ($arg) {
            case 'release':
                $debug = false;
                break;
            case 'asan':
                $asan = true;
                break;
            case 'ubsan':
                $ubsan = true;
                break;
            case 'bench':
                $configureFlags[] = '--enable-mbstring';
                $configureFlags[] = '--enable-sockets';
                $configureFlags[] = '--with-gmp';
                $configureFlags[] = '--with-mysqli=mysqlnd';
                $configureFlags[] = '--with-openssl';
                break;
            case 'valgrind':
                $configureFlags[] = '--with-valgrind';
                break;
            default:
                if (str_starts_with($arg, '--')) {
                    $configureFlags[] = $arg;
                } else if (preg_match('((?<name>\w+)=(?<value>.*))', $arg, $matches)) {
                    $envVars[$matches['name']] = $matches['value'];
                } else {
                    fprintf(STDERR, "Unknown argument %s\n", $arg);
                    exit(1);
                }
                break;
        }
    }

    if (isset($envVars['CFLAGS'])) {
        $envVars['CFLAGS'] .= ' -ggdb3';
    } else {
        $envVars['CFLAGS'] = '-ggdb3';
    }
    if ($debug) {
        $configureFlags[] = '--enable-debug';
    } else {
        $configureFlags[] = '--disable-debug';
        if (isset($envVars['CFLAGS'])) {
            $envVars['CFLAGS'] .= ' -O2';
        } else {
            $envVars['CFLAGS'] = '-O2';
        }
    }
    if ($asan) {
        $configureFlags[] = '--enable-address-sanitizer';
        $configureFlags[] = '--enable-undefined-sanitizer';
    }
    if ($ubsan) {
        $configureFlags[] = '--enable-undefined-sanitizer';
        $configureFlags[] = '--without-pcre-jit';
    }
    if (file_exists(ROOT . '/Makefile')) {
        runCommand(['make', 'distclean']);
    }
    runCommand(['./buildconf', '--force']);
    runCommand(['./configure', ...$configureFlags], envVars: $envVars);
    runCommand(['compiledb', 'make', '-j' . CORES]);
}

function test($args) {
    $testArgs = ['-j' . CORES, '-x', '-q', '-g FAIL,BORK', '--asan', '--show-diff'];
    $failed = false;

    foreach ($args as $arg) {
        switch ($arg) {
            case 'opcache':
                $testArgs[] = '-d opcache.enable_cli=1';
                break;
            case 'failed':
                $failed = true;
                break;
            default:
                $testArgs[] = $arg;
                break;
        }
    }

    $testArgs[] = $failed ? '-l' : '-w';
    $testArgs[] = ROOT . '/failed.txt';

    /* Compile separately to avoid excessive output. */
    runCommand(['make', '-j' . CORES]);
    runCommand(
        ['make', 'test', 'TESTS=' . implode(' ' , $testArgs)],
        /* NixOS sets the PHP_INI_SCAN_DIR env variable, but we don't want it here. */
        envVars: ['SKIP_IO_CAPTURE_TESTS' => '1', 'PHP_INI_SCAN_DIR' => ''],
        hideStdout: false);
}

$command = $argv[1] ?? null;

if ($command === null) {
    build();
} else if ($command === 'build') {
    rebuild(array_slice($argv, 2));
} else if ($command === 'test') {
    test(array_slice($argv, 2));
} else {
    fprintf(STDERR, "Unknown command '$command'\n");
    exit(1);
}
