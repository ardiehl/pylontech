# AD 3.12.2021: check for stale lock file, delete it if there is no related process
# AD 4.12.2021: strip /dev from device name means that e.g. /dev/ttyUSB1 works as well
LOCK_DIR=/var/lock/serial-starter

log() {
	echo "log: $*"
	logger -t "$(basename $0)" "$*"
}

lock_tty() {
        local N=$(echo $1 | sed "s/\/dev\///g")
	local LOCK_FN="$LOCK_DIR/$N"
        if [ -L "$LOCK_FN" ]; then
                local LOCK_PID=`readlink $LOCK_FN`
                if [ ! -d "/proc/$LOCK_PID" ]; then
                        log "$LOCK_FN: no process with pid $LOCK_PID, removing stale lock file"
                        rm -f "$LOCK_FN"
                fi
        fi
        ln -s $$ "$LOCK_FN" 2>/dev/null
}

unlock_tty() {
	local N=$(echo $1 | sed "s/\/dev\///g")
	rm -f "$LOCK_DIR/$N"
}

findpid() {
	PID=$(ps | grep -v grep | grep $1 | awk '{ print $1 }')
	[ -z "$PID" ] && return 1
	return 0
}

killprog() {
	if findpid $1; then
                kill $PID
                return
        else
                echo "can not find pid of $PROG"
                return 1
	fi
}
