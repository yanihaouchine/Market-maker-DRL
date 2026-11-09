#!/bin/bash
# Tue le processus qui écoute sur le port donné (8090 par défaut, port du dashboard).
PORT="${1:-8090}"
PID=$(lsof -ti TCP:"$PORT" -sTCP:LISTEN)
if [ -n "$PID" ]; then
  echo "Killing process on port $PORT (PID: $PID)"
  kill $PID
else
  echo "No process found on port $PORT."
fi
