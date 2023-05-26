# Reactd

Reactd is a small program that "tails" log files or systemd's journal, matches messages against regular expressions and takes configurable actions.

The regular expressions can optionally capture part of the message, and used when running external scripts.

It's generic enough to be usable in a number of applications, but the main use-case is to block hosts attempting brute force attacks to services such as SSH, SMTP, etc. But it could also be used in web applications, or anything.


## Requirements

- C compiler (tested with gcc)
- make (tested with GNU make)
- m4
- flex
- bison
- libpcre


## Building

To build, install the required packages and run `make`.

To build without systemd support (even when available), run `make NOSYSTEMD=1`.

For a static build, install the static versions of the libraries, then run `make STATIC=1`.

## Running

```
reactd [option]

Options:
 -c --config FILE    configuration file
 -t --test           syntax test configuration file and exit
 -p --pidfile FILE   pid file
 -d --logdst         one of: syslog, file, stdout, stderr
 -o --logfile        output file when logging to a file
 -l --loglevel       one of: emerg, alert, crit, err, warn, notice, info, debug
 -V --version        output version information and exit
 -h --help           display this help and exit

```

## Configuration File

For a more complete example, please refer to the sample config files in the repository.

Here are some short examples to get started:


```
"/var/log/sshd" {
    /authentication failure.*rhost=(\d+\.\d+\.\d+\.\d+)/ {
        command = "/usr/lib/reactd/sshd.sh" "block"
        key = "\1"
        trigger = 2 in 10 minutes
    }
    reset {
        timeout = 30 minutes
        command = "/usr/lib/reactd/sshd.sh" "reset"
    }
}
```

This would monitor /var/log/sshd, and whenever the regular expression matches, it will remember the IPv4 address of the matching line as the "key". Whenever the key matches 2 times in less than 10 minutes, it will trigger the execution of `/usr/lib/reactd/sshd.sh block`.
Once 30 minutes have passed for a particular key, the reset command will run: `/usr/lib/reactd/sshd.sh reset`.

When running an external script, the following environment variables will be available:
REACT_0 .. REACT_9: capture groups of the regular expression from the last match
REACT_KEY: the key that triggered the execution
REACT_SOURCE: filename where the string was matched, or "journal" for systemd's journal.

### Journal Example

```
journal SYSLOG_IDENTIFIER = php _UID = "5000" {
    /user (\S+) logged in from (\S+)/ {
        command = "/usr/local/bin/www-login.sh" "\1" "\2"
    }
    /user (\S+) logged out/ {
        command = "/usr/local/bin/www-logout.sh" "\1"
    }
    /user (\S+) failed authentication/ {
        command = "/usr/local/bin/www-alert.sh" "\1"
        key = "\1"
        trigger = 2 in 10 minutes
    }
}
```

This would monitor the journal, and react to all journal entries where all the fields match (eg: SYSLOG_IDENTIFIER = php AND _UID = 5000).
To inspect the fields in the journal, you can run `journalctl -o verbose`.
See also the man page of `systemd.journal-fields`.

As can be seen, a single file/journal block may have multiple regular expression blocks.
The `trigger` is optional, but if present, there must be a `key`.
The `reset` block is also optional, but if present, there must be a `timeout` and `command`.

## Notes

Reactd doesn't require systemd to build or run, it's completely optional.

It's designed to be lightweight, and as such should work well in embedded systems.

I'm working on this code in my spare time as a hobby, and it's still work-in-progress. Please take that into consideration before deploying in hosts exposed to the Internet. That said, any vulnerabilities reported will be dealt with as soon as possible.


## Acknowledgements

This project uses code from other projects and wouldn't be possible without their contributions.

In particular libpcre, libavl and CuTest.
