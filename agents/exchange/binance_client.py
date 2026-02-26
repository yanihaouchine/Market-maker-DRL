"""
Binance Testnet Client for direct order execution
"""

import hmac
import hashlib
import time
import requests
from typing import Dict, Optional
from ..trading_types import Side


class BinanceTestnetClient:
    """Client for Binance Testnet API"""
    
    def __init__(self, api_key: str, api_secret: str):
        self.api_key = api_key
        self.api_secret = api_secret
        self.base_url = "https://testnet.binance.vision"
        self.session = requests.Session()
        self.session.headers.update({
            'X-MBX-APIKEY': self.api_key
        })
        
    def _sign_request(self, params: Dict) -> str:
        """Generate signature for authenticated requests"""
        query_string = '&'.join([f"{k}={v}" for k, v in params.items()])
        signature = hmac.new(
            self.api_secret.encode('utf-8'),
            query_string.encode('utf-8'),
            hashlib.sha256
        ).hexdigest()
        return signature
    
    def get_account_info(self) -> Optional[Dict]:
        """Get account information"""
        try:
            endpoint = "/api/v3/account"
            params = {
                'timestamp': int(time.time() * 1000)
            }
            params['signature'] = self._sign_request(params)
            
            response = self.session.get(
                f"{self.base_url}{endpoint}",
                params=params,
                timeout=10
            )
            response.raise_for_status()
            return response.json()
        except Exception as e:
            print(f"❌ Error getting account info: {e}")
            return None
    
    def get_balance(self, asset: str = "USDT") -> Optional[float]:
        """Get balance for a specific asset"""
        account_info = self.get_account_info()
        if not account_info:
            return None
        
        for balance in account_info.get('balances', []):
            if balance['asset'] == asset:
                return float(balance['free'])
        return None
    
    def place_order(self, symbol: str, side: Side, quantity: float, 
                   order_type: str = "MARKET") -> Optional[Dict]:
        """Place an order on Binance testnet
        
        Args:
            symbol: Trading pair (e.g., "SOLUSDT")
            side: BUY or SELL
            quantity: Quantity to trade
            order_type: MARKET, LIMIT, etc.
        
        Returns:
            Order response or None if failed
        """
        try:
            endpoint = "/api/v3/order"
            
            params = {
                'symbol': symbol,
                'side': side.value.upper(),
                'type': order_type,
                'quantity': f"{quantity:.8f}".rstrip('0').rstrip('.'),
                'timestamp': int(time.time() * 1000)
            }
            
            params['signature'] = self._sign_request(params)
            
            response = self.session.post(
                f"{self.base_url}{endpoint}",
                params=params,
                timeout=10
            )
            
            if response.status_code == 200:
                return response.json()
            else:
                print(f"❌ Order failed: {response.status_code} - {response.text}")
                return None
                
        except Exception as e:
            print(f"❌ Error placing order: {e}")
            return None
    
    def place_limit_order(self, symbol: str, side: Side, quantity: float, 
                         price: float) -> Optional[Dict]:
        """Place a limit order
        
        Args:
            symbol: Trading pair (e.g., "SOLUSDT")
            side: BUY or SELL
            quantity: Quantity to trade
            price: Limit price
        
        Returns:
            Order response or None if failed
        """
        try:
            endpoint = "/api/v3/order"
            
            params = {
                'symbol': symbol,
                'side': side.value.upper(),
                'type': 'LIMIT',
                'timeInForce': 'GTC',  # Good Till Cancelled
                'quantity': f"{quantity:.8f}".rstrip('0').rstrip('.'),
                'price': f"{price:.8f}".rstrip('0').rstrip('.'),
                'timestamp': int(time.time() * 1000)
            }
            
            params['signature'] = self._sign_request(params)
            
            response = self.session.post(
                f"{self.base_url}{endpoint}",
                params=params,
                timeout=10
            )
            
            if response.status_code == 200:
                return response.json()
            else:
                print(f"❌ Limit order failed: {response.status_code} - {response.text}")
                return None
                
        except Exception as e:
            print(f"❌ Error placing limit order: {e}")
            return None
    
    def get_order_status(self, symbol: str, order_id: int) -> Optional[Dict]:
        """Get status of a specific order"""
        try:
            endpoint = "/api/v3/order"
            params = {
                'symbol': symbol,
                'orderId': order_id,
                'timestamp': int(time.time() * 1000)
            }
            params['signature'] = self._sign_request(params)
            
            response = self.session.get(
                f"{self.base_url}{endpoint}",
                params=params,
                timeout=10
            )
            response.raise_for_status()
            return response.json()
        except Exception as e:
            print(f"❌ Error getting order status: {e}")
            return None
    
    def cancel_order(self, symbol: str, order_id: int) -> Optional[Dict]:
        """Cancel an open order"""
        try:
            endpoint = "/api/v3/order"
            params = {
                'symbol': symbol,
                'orderId': order_id,
                'timestamp': int(time.time() * 1000)
            }
            params['signature'] = self._sign_request(params)
            
            response = self.session.delete(
                f"{self.base_url}{endpoint}",
                params=params,
                timeout=10
            )
            response.raise_for_status()
            return response.json()
        except Exception as e:
            print(f"❌ Error canceling order: {e}")
            return None
    
    def get_open_orders(self, symbol: Optional[str] = None) -> Optional[list]:
        """Get all open orders"""
        try:
            endpoint = "/api/v3/openOrders"
            params = {
                'timestamp': int(time.time() * 1000)
            }
            if symbol:
                params['symbol'] = symbol
                
            params['signature'] = self._sign_request(params)
            
            response = self.session.get(
                f"{self.base_url}{endpoint}",
                params=params,
                timeout=10
            )
            response.raise_for_status()
            return response.json()
        except Exception as e:
            print(f"❌ Error getting open orders: {e}")
            return None
    
    def test_connectivity(self) -> bool:
        """Test connection to Binance testnet"""
        try:
            response = self.session.get(f"{self.base_url}/api/v3/ping", timeout=5)
            return response.status_code == 200
        except:
            return False
