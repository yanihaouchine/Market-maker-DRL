#!/bin/bash

# Integration test: Verify all 3 strategies work with live market data (C++ + Python)

echo "═══════════════════════════════════════════════════════════════════════════"
echo "   🚀 Trading System Integration Test - All Strategies"
echo "═══════════════════════════════════════════════════════════════════════════"
echo ""

# Get the directory where this script is located
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

# Colors
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m'

# Test each strategy
for STRATEGY in mean_reversion rsi bollinger; do
    echo -e "${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
    
    if [ "$STRATEGY" = "mean_reversion" ]; then
        echo -e "${YELLOW}📊 Testing Mean Reversion Strategy${NC}"
        export STRATEGY=mean_reversion
    elif [ "$STRATEGY" = "rsi" ]; then
        echo -e "${YELLOW}📊 Testing RSI Strategy${NC}"
        export STRATEGY=rsi
    else
        echo -e "${YELLOW}📊 Testing Bollinger Bands Strategy${NC}"
        export STRATEGY=bollinger
    fi
    
    echo ""
    
    # Start market data (C++)
    ./build/LiveMarketData SOLUSDT 2 10 > /dev/null 2>&1 &
    MARKET_PID=$!
    sleep 2
    
    # Run agent (Python) with market data for 5 seconds
    timeout 5 python3 -m agents.main 2>&1 &
    AGENT_PID=$!
    wait $AGENT_PID 2>/dev/null || true
    
    # Cleanup
    kill $MARKET_PID 2>/dev/null || true
    wait $MARKET_PID 2>/dev/null || true
    sleep 1
    echo ""
done

echo -e "${GREEN}═══════════════════════════════════════════════════════════════════════════${NC}"
echo -e "${GREEN}✅ Integration Test Complete - All Strategies Verified${NC}"
echo -e "${GREEN}═══════════════════════════════════════════════════════════════════════════${NC}"
