#!/usr/bin/env python3
"""
Launch Dreamer v3 training with live metrics display.

Usage:
    python3 run_dreamer.py              # default settings, live plot
    python3 run_dreamer.py --fast       # CPU-friendly: smaller batch, shorter sequences (~10s/ep)
    python3 run_dreamer.py --epochs 500 # custom epoch count
    python3 run_dreamer.py --no-plot    # headless (save PNG only)
"""

import argparse
import io
import sys
import zipfile
from pathlib import Path
from urllib.request import urlopen

from agents.train_dreamer import train_dreamer

DATA_DIR = Path(__file__).resolve().parent / "data"
CSV_FILE = DATA_DIR / "SOLUSDT-aggTrades-2026-02.csv"

BINANCE_URL = (
    "https://data.binance.vision/data/spot/monthly/aggTrades/"
    "SOLUSDT/SOLUSDT-aggTrades-2026-02.zip"
)


def ensure_data():
    if CSV_FILE.exists():
        return
    DATA_DIR.mkdir(parents=True, exist_ok=True)
    print("[Data] Downloading SOLUSDT aggTrades from Binance…")
    try:
        resp = urlopen(BINANCE_URL)
        buf = io.BytesIO(resp.read())
        with zipfile.ZipFile(buf) as zf:
            csv_name = [n for n in zf.namelist() if n.endswith(".csv")][0]
            zf.extract(csv_name, str(DATA_DIR))
            extracted = DATA_DIR / csv_name
            if extracted != CSV_FILE:
                extracted.rename(CSV_FILE)
        print(f"[Data] Saved to {CSV_FILE}  ({CSV_FILE.stat().st_size / 1e6:.1f} MB)")
    except Exception as e:
        print(f"[Data] Auto-download failed: {e}")
        print(f"[Data] Please manually download and place CSV at:\n       {CSV_FILE}")
        sys.exit(1)



def main():
    parser = argparse.ArgumentParser(description="Dreamer v3 Training")
    parser.add_argument("--epochs", type=int, default=2000, help="Number of training epochs")
    parser.add_argument("--batch-size", type=int, default=None)
    parser.add_argument("--seq-len", type=int, default=None)
    parser.add_argument("--grad-steps", type=int, default=None)
    parser.add_argument("--no-plot", action="store_true", help="Disable live plot window")
    parser.add_argument("--fast", action="store_true",
                        help="CPU-friendly settings: batch=8, seq=16, grad_steps=2 (~10s/ep)")
    args = parser.parse_args()

    if args.fast:
        batch_size = args.batch_size or 8
        seq_len = args.seq_len or 16
        grad_steps = args.grad_steps or 2
    else:
        batch_size = args.batch_size or 16
        seq_len = args.seq_len or 64
        grad_steps = args.grad_steps or 4

    train_dreamer(
        n_epochs=args.epochs,
        batch_size=batch_size,
        seq_len=seq_len,
        grad_steps=grad_steps,
        live_plot=not args.no_plot,
    )


if __name__ == "__main__":
    ensure_data()
    main()
