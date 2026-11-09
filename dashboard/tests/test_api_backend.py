import os

import pytest
import requests

BASE_URL = os.getenv("DASHBOARD_URL", "http://127.0.0.1:8090")
USER = os.getenv("DASHBOARD_USER", "admin")
PASSWORD = os.getenv("DASHBOARD_PASSWORD", "admin")


@pytest.fixture(scope="module")
def session():
    try:
        requests.get(BASE_URL + "/login", timeout=2)
    except requests.ConnectionError:
        pytest.skip(f"dashboard server not running on {BASE_URL}")
    s = requests.Session()
    resp = s.post(BASE_URL + "/api/login", json={"username": USER, "password": PASSWORD})
    assert resp.status_code == 200, resp.text
    return s


def test_requires_login():
    try:
        resp = requests.get(BASE_URL + "/api/status", allow_redirects=False, timeout=2)
    except requests.ConnectionError:
        pytest.skip(f"dashboard server not running on {BASE_URL}")
    assert resp.status_code == 302
    assert resp.headers["Location"] == "/login"


@pytest.mark.parametrize("endpoint", [
    "/snapshot",
    "/reset_status",
    "/market_state",
    "/trade_log",
    "/performance",
    "/chart",
    "/status",
    "/metrics",
    "/recent_logs",
])
def test_api_endpoints_available(session, endpoint):
    resp = session.get(BASE_URL + "/api" + endpoint)
    assert resp.status_code == 200, f"{endpoint} not available"
    assert resp.headers["content-type"].startswith("application/json")


def test_market_state_content(session):
    data = session.get(BASE_URL + "/api/market_state").json()
    assert "ok" in data
    assert "symbol" in data
    if data["ok"]:
        assert "ask_price" in data
        assert "bid_price" in data


def test_start_and_stop_simulation(session):
    resp = session.post(BASE_URL + "/api/start_simulation", json={"product": "SOLUSDT", "agent": "Mean Reversion"})
    assert resp.status_code == 200
    resp = session.post(BASE_URL + "/api/stop_simulation")
    assert resp.status_code == 200


def test_clear_logs(session):
    resp = session.post(BASE_URL + "/api/clear_logs")
    assert resp.status_code == 200
    assert resp.json().get("ok") is not None
