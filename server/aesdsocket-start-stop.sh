case "$1" in
  start)
    echo "Starting aesdsocket server"
    start-stop-daemon -S -n aesdsocket -a /usr/bin/aesdsocket -- -d
    start-stop-daemon -S -n aesdchar_load -a /usr/bin/aesdchar_load
    ;;
  stop)
    echo "Stopping aesdsocket server"
    start-stop-daemon -K -n aesdsocket
    start-stop-daemon -K -n aesdchar_load
    ;;
  *)
    
    echo "Usage: $0 {start|stop}"
    
    exit 1
    
esac

exit 0
