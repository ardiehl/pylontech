# AD 3.12.2021: check for stale lock file, delete it if there is no related process
LOCK_DIR=/var/lock/serial-starter
            

lock_tty() {                           
        local LOCK_FN="$LOCK_DIR/$1"       
        if [ -L "$LOCK_FN" ]; then                
                local LOCK_PID=`readlink $LOCK_FN`      
#               echo "$LOCK_FN exists, pid is $LOCK_PID"
                if [ ! -d "/proc/$LOCK_PID" ]; then                                             
                        echo "$LOCK_FN: looks like there is no process with pid $LOCK_PID, removing stale lock file"
                        rm -f "$LOCK_FN"
                fi                     
        fi                             
        ln -s $$ "$LOCK_FN" 2>/dev/null
}                       
                        
unlock_tty() {          
    rm -f "$LOCK_DIR/$1"
}                   

log() {
	echo "log: $*"
	logger -t "$(basename $BASH_SOURCE)" "$*" 
}
