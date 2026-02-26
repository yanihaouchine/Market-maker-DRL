#!/bin/bash

# Script pour visualiser les ordres via l'API Binance testnet avec curl

# Charger les credentials
API_KEY="YOUR_API_KEY"
API_SECRET="YOUR_API_SECRET"

SYMBOL=${1:-SOLUSDT}
LIMIT=${2:-10}

echo "📡 Requête API manuelle à Binance testnet"
echo "Symbol: $SYMBOL"
echo "Limit: $LIMIT"
echo

# Timestamp
TIMESTAMP=$(date +%s%3N)

# Construire la query string
QUERY_STRING="symbol=${SYMBOL}&limit=${LIMIT}&timestamp=${TIMESTAMP}"

# Générer la signature HMAC-SHA256
SIGNATURE=$(echo -n "$QUERY_STRING" | openssl dgst -sha256 -hmac "$API_SECRET" | awk '{print $2}')

# URL complète
URL="https://testnet.binance.vision/api/v3/allOrders?${QUERY_STRING}&signature=${SIGNATURE}"

echo "🔗 URL de requête:"
echo "$URL"
echo

# Faire la requête
echo "📥 Réponse de l'API:"
echo
curl -s -H "X-MBX-APIKEY: $API_KEY" "$URL" | python3 -m json.tool

echo
echo "✅ Requête terminée"
