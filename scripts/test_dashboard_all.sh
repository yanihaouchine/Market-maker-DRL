#!/bin/bash
# Lance le serveur dashboard, attend qu'il réponde, puis exécute les tests API et E2E.
# PYTHON permet de choisir l'interpréteur (défaut : eval_venv/bin/python s'il existe).

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$PROJECT_ROOT" || exit 1

PORT=8090
URL="http://127.0.0.1:$PORT"
PYTHON="${PYTHON:-$( [ -x eval_venv/bin/python ] && echo eval_venv/bin/python || echo python3 )}"
SERVER_LOG="$(mktemp)"

bash scripts/kill_port.sh "$PORT"

"$PYTHON" dashboard/server.py > "$SERVER_LOG" 2>&1 &
SERVER_PID=$!
trap 'kill $SERVER_PID 2>/dev/null; rm -f "$SERVER_LOG"' EXIT

for i in {1..15}; do
  if curl -s "$URL/login" > /dev/null; then
    echo "[OK] Dashboard server is up."
    break
  fi
  if ! kill -0 $SERVER_PID 2>/dev/null; then
    echo "[FAIL] Le serveur dashboard a crashé au démarrage :"
    cat "$SERVER_LOG"
    exit 1
  fi
  echo "[WAIT] Waiting for dashboard server... ($i)"
  sleep 1
done

if ! curl -s "$URL/login" > /dev/null; then
  echo "[FAIL] Le serveur ne répond pas après 15 s :"
  cat "$SERVER_LOG"
  exit 1
fi

DASHBOARD_URL="$URL" "$PYTHON" -m pytest dashboard/tests/test_api_backend.py
API_STATUS=$?

if [ -x node_modules/.bin/playwright ]; then
  DASHBOARD_URL="$URL" npx playwright test
  E2E_STATUS=$?
else
  echo "[WARN] Playwright n'est pas installé (npm ci && npx playwright install). Tests E2E ignorés."
  E2E_STATUS=0
fi

if [ $API_STATUS -eq 0 ] && [ $E2E_STATUS -eq 0 ]; then
  echo "Tous les tests sont passés."
else
  echo "Certains tests ont échoué."
  exit 1
fi
