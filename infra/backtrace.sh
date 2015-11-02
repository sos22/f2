#! /bin/bash

if [ $# != 1 ]
then
    echo "need a single arguments, the log file to parse"
    exit 1
fi

logfile=$1

decodebacktrace() {
    echo "BACKTRACE"
    sed 's,\([0-9a-zA-Z_/-]*\)(.*) \[\(0x[0-9a-f]*\)\],\1 \2,' \
        | while read binary addr
          do
              echo "-----"
              addr2line -fie $binary $addr | c++filt
          done
}

grep --context 5 "BACKTRACE" $logfile | while read line
                                        do
                                            if ! (echo "$line" | grep -q BACKTRACE)
                                            then
                                                echo "$line"
                                                continue
                                            fi
                                            echo $line | sed 's/{BACKTRACE.*}/BACKTRACE/'
                                            echo $line | sed 's/.*{BACKTRACE \(.*\)}.*/\1/' | tr ';' '\n' | decodebacktrace | sed 's/^/>>> /'
                                        done
