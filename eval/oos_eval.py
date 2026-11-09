"""
Évaluation hors-échantillon de tous les agents (règles + RL) sur SOLUSDT mars 2026.

Tous les agents sont mis dans un environnement directionnel discret commun
(actions {0=SELL, 1=HOLD, 2=BUY}, PnL réalisé via FIFO long/short, MTM final).

Usage:
    eval_venv/bin/python eval/oos_eval.py
"""

from __future__ import annotations

import sys
from collections import deque
from pathlib import Path
from typing import Dict, List

import numpy as np
import pandas as pd
import torch
import torch.nn as nn
import torch.nn.functional as F

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))

CSV_TEST = ROOT / "data" / "SOLUSDT-aggTrades-2026-03.csv"
MODELS = ROOT / "models"

WINDOW = 30
MAX_INV = 3
MAX_STEPS = 200
N_EPISODES = 50
SEED = 42


def build_volume_bars(csv_path, volume_threshold=1000):
    print(f"[data] lecture {csv_path.name}")
    df = pd.read_csv(
        csv_path, header=None,
        names=["id", "price", "qty", "f", "l", "timestamp", "m", "M"],
        low_memory=False,
    )
    df["price"] = pd.to_numeric(df["price"], errors="coerce")
    df["qty"] = pd.to_numeric(df["qty"], errors="coerce")
    df = df.dropna(subset=["price", "qty"])
    df["cum_vol"] = df["qty"].cumsum()
    df["bar_id"] = df["cum_vol"] // volume_threshold
    ohlcv = df.groupby("bar_id").agg(
        open=("price", "first"),
        high=("price", "max"),
        low=("price", "min"),
        close=("price", "last"),
        volume=("qty", "sum"),
    )
    if ohlcv.iloc[-1]["volume"] < volume_threshold * 0.9:
        ohlcv = ohlcv.iloc[:-1]
    print(f"[data] {len(ohlcv):,} volume bars")
    return ohlcv


class TradingEnv:
    """
    Actions: 0=SELL, 1=HOLD, 2=BUY.
    Inventaire borné à [-MAX_INV, MAX_INV]. Long et short avec FIFO.
    Récompense pas-à-pas = PnL réalisé sur la fermeture éventuelle de la position.
    PnL final = somme PnL réalisé + MTM des positions ouvertes au prix de clôture.
    Observation 121-dim alignée sur PPO/A2C/DQN/Dreamer:
      [norm_close(30), norm_high(30), norm_low(30), norm_volume(30), inv/MAX_INV]
    """

    def __init__(self, bars: pd.DataFrame, window: int = WINDOW,
                 max_steps: int = MAX_STEPS, max_inv: int = MAX_INV):
        self.closes = bars["close"].values.astype(np.float32)
        self.highs = bars["high"].values.astype(np.float32)
        self.lows = bars["low"].values.astype(np.float32)
        self.volumes = bars["volume"].values.astype(np.float32)
        self.window = window
        self.max_steps = max_steps
        self.max_inv = max_inv
        self.n = len(self.closes)

    def reset(self, start: int):
        self.cur = int(start)
        self.steps = 0
        self.inv = 0
        self.realized = 0.0
        self.long_basis: deque = deque()
        self.short_basis: deque = deque()
        self.trades = 0
        return self._obs()

    def _obs(self) -> np.ndarray:
        i, w = self.cur, self.window
        c = self.closes[i - w:i]
        h = self.highs[i - w:i]
        lo = self.lows[i - w:i]
        v = self.volumes[i - w:i]
        base = c[-1] + 1e-8
        norm_c = (c / base) - 1.0
        norm_h = (h / base) - 1.0
        norm_lo = (lo / base) - 1.0
        norm_v = (v / (v.mean() + 1e-8)) - 1.0
        norm_inv = self.inv / self.max_inv
        return np.concatenate(
            [norm_c, norm_h, norm_lo, norm_v, [norm_inv]]
        ).astype(np.float32)

    def step(self, action: int):
        price = float(self.closes[self.cur])
        self.cur += 1
        realized = 0.0

        if action == 2:  # BUY
            if self.inv < 0 and self.short_basis:
                entry = self.short_basis.popleft()
                realized = entry - price
                self.inv += 1
                self.trades += 1
            elif self.inv < self.max_inv:
                self.inv += 1
                self.long_basis.append(price)
                self.trades += 1
        elif action == 0:  # SELL
            if self.inv > 0 and self.long_basis:
                entry = self.long_basis.popleft()
                realized = price - entry
                self.inv -= 1
                self.trades += 1
            elif self.inv > -self.max_inv:
                self.inv -= 1
                self.short_basis.append(price)
                self.trades += 1

        self.realized += realized
        self.steps += 1
        done = self.steps >= self.max_steps
        return self._obs(), realized, done, {"price": price}

    def close_pnl(self) -> float:
        last = float(self.closes[self.cur])
        mtm = 0.0
        for entry in self.long_basis:
            mtm += last - entry
        for entry in self.short_basis:
            mtm += entry - last
        return self.realized + mtm


class Agent:
    name: str = "BASE"
    requires_warmup: bool = False

    def reset(self):
        pass

    def act(self, obs: np.ndarray, price: float, inv: int) -> int:
        return 1


class RandomAgent(Agent):
    name = "Random"

    def __init__(self, p_buy: float = 0.1, p_sell: float = 0.1, seed: int = 0):
        self.p_buy = p_buy
        self.p_sell = p_sell
        self.rng = np.random.default_rng(seed)

    def act(self, obs, price, inv):
        u = self.rng.random()
        if u < self.p_buy:
            return 2
        if u > 1.0 - self.p_sell:
            return 0
        return 1


class BollingerAgent(Agent):
    name = "Bollinger"

    def __init__(self, period: int = 20, std_mult: float = 2.0):
        self.period = period
        self.std_mult = std_mult
        self.prices: deque = deque(maxlen=period)

    def reset(self):
        self.prices.clear()

    def act(self, obs, price, inv):
        self.prices.append(price)
        if len(self.prices) < self.period:
            return 1
        arr = np.fromiter(self.prices, dtype=np.float32)
        sma = arr.mean()
        sd = arr.std()
        upper = sma + self.std_mult * sd
        lower = sma - self.std_mult * sd
        if price <= lower:
            return 2
        if price >= upper:
            return 0
        return 1


class SMAAgent(Agent):
    name = "SMA"

    def __init__(self, window: int = 10, threshold: float = 0.001):
        self.window = window
        self.threshold = threshold
        self.prices: deque = deque(maxlen=window)

    def reset(self):
        self.prices.clear()

    def act(self, obs, price, inv):
        self.prices.append(price)
        if len(self.prices) < 3:
            return 1
        sma = float(np.mean(self.prices))
        if sma == 0:
            return 1
        dev = (price - sma) / sma
        if dev < -self.threshold:
            return 2
        if dev > self.threshold:
            return 0
        return 1


class MeanReversionAgent(Agent):
    name = "MeanReversion"

    def __init__(self, window: int = 10, threshold_pct: float = 0.01):
        self.window = window
        self.threshold = threshold_pct
        self.prices: deque = deque(maxlen=window)

    def reset(self):
        self.prices.clear()

    def act(self, obs, price, inv):
        self.prices.append(price)
        if len(self.prices) < 3:
            return 1
        sma = float(np.mean(self.prices))
        if sma == 0:
            return 1
        dev = (price - sma) / sma * 100.0
        if dev < -self.threshold:
            return 2
        if dev > self.threshold:
            return 0
        return 1


class RSIAgent(Agent):
    name = "RSI"

    def __init__(self, period: int = 14):
        self.period = period
        self.prices: deque = deque(maxlen=period + 1)

    def reset(self):
        self.prices.clear()

    def act(self, obs, price, inv):
        self.prices.append(price)
        if len(self.prices) < self.period + 1:
            return 1
        arr = np.fromiter(self.prices, dtype=np.float32)
        deltas = np.diff(arr)
        gains = float(deltas[deltas > 0].sum()) / self.period
        losses = float(-deltas[deltas < 0].sum()) / self.period
        if losses == 0:
            rsi = 100.0 if gains > 0 else 50.0
        else:
            rs = gains / losses
            rsi = 100.0 - 100.0 / (1.0 + rs)
        if rsi < 30:
            return 2
        if rsi > 70:
            return 0
        return 1


class _DQNNet(nn.Module):
    """Architecture identique à dqn_agent_faster._QNet"""

    def __init__(self):
        super().__init__()
        self.net = nn.Sequential(
            nn.Linear(121, 128), nn.ReLU(),
            nn.Linear(128, 128), nn.ReLU(),
            nn.Linear(128, 3),
        )

    def forward(self, x):
        return self.net(x)


class _ActorNN(nn.Module):
    """Architecture identique à a2c_rl.ActorNN (state_dim=121, action_dim=3)"""

    def __init__(self):
        super().__init__()
        self.backbone = nn.Sequential(
            nn.Linear(121, 512), nn.LayerNorm(512), nn.ReLU(),
            nn.Linear(512, 256), nn.LayerNorm(256), nn.ReLU(),
        )
        self.actor_head = nn.Linear(256, 3)

    def forward(self, x):
        return self.actor_head(self.backbone(x))


class _PPOPolicyNet(nn.Module):
    """Architecture identique à train_ppo.PolicyNetwork (input_dim=121, output 2 tanh)"""

    def __init__(self):
        super().__init__()
        self.net = nn.Sequential(
            nn.Linear(121, 64), nn.ReLU(),
            nn.Linear(64, 64), nn.ReLU(),
            nn.Linear(64, 2), nn.Tanh(),
        )

    def forward(self, x):
        return self.net(x)


class _DreamerEncoder(nn.Module):
    def __init__(self, obs_dim=121, embed_dim=256):
        super().__init__()
        self.net = nn.Sequential(
            nn.Linear(obs_dim, 512), nn.LayerNorm(512), nn.SiLU(),
            nn.Linear(512, embed_dim), nn.LayerNorm(embed_dim), nn.SiLU(),
        )

    def forward(self, x):
        return self.net(x)


class _DreamerRSSM(nn.Module):
    def __init__(self, action_dim=3, embed_dim=256, deter_dim=512,
                 stoch_classes=32, stoch_categories=32):
        super().__init__()
        self.deter_dim = deter_dim
        self.stoch_classes = stoch_classes
        self.stoch_categories = stoch_categories
        self.stoch_dim = stoch_classes * stoch_categories
        self.gru_input = nn.Sequential(
            nn.Linear(self.stoch_dim + action_dim, deter_dim),
            nn.LayerNorm(deter_dim), nn.SiLU(),
        )
        self.gru_cell = nn.GRUCell(deter_dim, deter_dim)
        self.prior_net = nn.Sequential(
            nn.Linear(deter_dim, 256), nn.LayerNorm(256), nn.SiLU(),
            nn.Linear(256, self.stoch_dim),
        )
        self.posterior_net = nn.Sequential(
            nn.Linear(deter_dim + embed_dim, 256), nn.LayerNorm(256), nn.SiLU(),
            nn.Linear(256, self.stoch_dim),
        )

    def initial_state(self, batch, device):
        h = torch.zeros(batch, self.deter_dim, device=device)
        z = torch.zeros(batch, self.stoch_dim, device=device)
        return h, z

    def _sample_st(self, logits):
        probs = F.softmax(logits, dim=-1)
        idx = torch.argmax(probs, dim=-1)
        sample = F.one_hot(idx, self.stoch_classes).float()
        return (sample + probs - probs.detach()).flatten(-2)

    def observe_step(self, h, z, action, embed):
        inp = self.gru_input(torch.cat([z, action], dim=-1))
        h_new = self.gru_cell(inp, h)
        post_logits = self.posterior_net(
            torch.cat([h_new, embed], dim=-1)
        ).view(-1, self.stoch_categories, self.stoch_classes)
        z_new = self._sample_st(post_logits)
        return h_new, z_new


class _DreamerActor(nn.Module):
    def __init__(self, state_dim=1536, action_dim=3, unimix=0.01):
        super().__init__()
        self.action_dim = action_dim
        self.unimix = unimix
        self.net = nn.Sequential(
            nn.Linear(state_dim, 256), nn.LayerNorm(256), nn.SiLU(),
            nn.Linear(256, 128), nn.LayerNorm(128), nn.SiLU(),
            nn.Linear(128, action_dim),
        )

    def forward(self, x):
        logits = self.net(x)
        probs = (1.0 - self.unimix) * F.softmax(logits, dim=-1) \
                + self.unimix / self.action_dim
        return probs


class DQNRLAgent(Agent):
    name = "DQN"

    def __init__(self, model_path):
        self.net = _DQNNet()
        sd = torch.load(model_path, map_location="cpu", weights_only=True)
        self.net.load_state_dict(sd)
        self.net.eval()

    def act(self, obs, price, inv):
        with torch.no_grad():
            q = self.net(torch.tensor(obs).unsqueeze(0))
        return int(q.argmax(dim=-1).item())


class A2CRLAgent(Agent):
    name = "A2C"

    def __init__(self, actor_path):
        self.actor = _ActorNN()
        sd = torch.load(actor_path, map_location="cpu", weights_only=True)
        self.actor.load_state_dict(sd)
        self.actor.eval()

    def act(self, obs, price, inv):
        with torch.no_grad():
            logits = self.actor(torch.tensor(obs).unsqueeze(0))
        return int(logits.argmax(dim=-1).item())


class PPORLAgent(Agent):
    """
    PPO a été entraîné en market-maker avec actions continues
    (ask_distance, bid_distance) ∈ [-1, 1]^2. On la convertit en
    décision directionnelle: la jambe avec la plus PETITE distance
    indique l'ordre que PPO veut "remplir vite".
      ask <  bid → veut vendre (ordre vente posté serré)  → SELL
      bid <  ask → veut acheter                            → BUY
      ≈ égales   → HOLD
    """
    name = "PPO"

    def __init__(self, policy_path, hold_threshold: float = 0.1):
        self.net = _PPOPolicyNet()
        sd = torch.load(policy_path, map_location="cpu", weights_only=True)
        self.net.load_state_dict(sd)
        self.net.eval()
        self.hold_th = hold_threshold

    def act(self, obs, price, inv):
        with torch.no_grad():
            a = self.net(torch.tensor(obs).unsqueeze(0)).squeeze(0).numpy()
        # a[0] = ask offset, a[1] = bid offset (∈ [-1,1])
        diff = float(a[0]) - float(a[1])
        if abs(diff) < self.hold_th:
            return 1
        return 0 if diff < 0 else 2


class DreamerRLAgent(Agent):
    name = "Dreamer"

    def __init__(self, ckpt_path):
        self.encoder = _DreamerEncoder()
        self.rssm = _DreamerRSSM()
        self.actor = _DreamerActor()
        ckpt = torch.load(ckpt_path, map_location="cpu", weights_only=True)
        self.encoder.load_state_dict(ckpt["encoder"])
        self.rssm.load_state_dict(ckpt["rssm"])
        self.actor.load_state_dict(ckpt["actor"])
        self.encoder.eval()
        self.rssm.eval()
        self.actor.eval()
        self.h = None
        self.z = None
        self.prev_action = None

    def reset(self):
        self.h, self.z = self.rssm.initial_state(1, torch.device("cpu"))
        self.prev_action = torch.zeros(1, 3)

    def act(self, obs, price, inv):
        with torch.no_grad():
            obs_t = torch.tensor(obs).unsqueeze(0)
            embed = self.encoder(obs_t)
            self.h, self.z = self.rssm.observe_step(
                self.h, self.z, self.prev_action, embed
            )
            state = torch.cat([self.h, self.z], dim=-1)
            probs = self.actor(state)
            action = int(probs.argmax(dim=-1).item())
            self.prev_action = F.one_hot(
                torch.tensor([action]), num_classes=3
            ).float()
        return action


def run_episode(env: TradingEnv, agent: Agent, start: int) -> Dict:
    obs = env.reset(start=start)
    agent.reset()
    actions = [0, 0, 0]  # SELL, HOLD, BUY counts
    while True:
        a = agent.act(obs, float(env.closes[env.cur]), env.inv)
        actions[a] += 1
        obs, _, done, _ = env.step(a)
        if done:
            break
    pnl = env.close_pnl()
    return {
        "pnl": pnl,
        "trades": env.trades,
        "final_inv": env.inv,
        "n_sell": actions[0],
        "n_hold": actions[1],
        "n_buy": actions[2],
    }


def evaluate(env: TradingEnv, agent: Agent, n_episodes: int = N_EPISODES,
             seed: int = SEED) -> List[Dict]:
    rng = np.random.default_rng(seed)
    max_start = env.n - env.max_steps - 2
    starts = rng.integers(env.window, max_start, size=n_episodes)
    rows = []
    for ep, s in enumerate(starts, 1):
        r = run_episode(env, agent, int(s))
        rows.append(r)
    return rows


def summarise(name: str, rows: List[Dict]) -> Dict:
    pnls = np.array([r["pnl"] for r in rows], dtype=np.float64)
    trades = np.array([r["trades"] for r in rows], dtype=np.float64)
    sharpe = pnls.mean() / (pnls.std() + 1e-9) * np.sqrt(len(pnls))
    n_sell = sum(r["n_sell"] for r in rows)
    n_hold = sum(r["n_hold"] for r in rows)
    n_buy = sum(r["n_buy"] for r in rows)
    tot = n_sell + n_hold + n_buy
    return {
        "agent": name,
        "pnl_mean": pnls.mean(),
        "pnl_median": np.median(pnls),
        "pnl_std": pnls.std(),
        "pnl_min": pnls.min(),
        "pnl_max": pnls.max(),
        "win_rate_%": 100.0 * (pnls > 0).mean(),
        "sharpe": sharpe,
        "trades_mean": trades.mean(),
        "%_buy": 100.0 * n_buy / max(tot, 1),
        "%_sell": 100.0 * n_sell / max(tot, 1),
        "%_hold": 100.0 * n_hold / max(tot, 1),
    }


def build_agents():
    agents = []
    # Règles
    agents.append(RandomAgent(seed=0))
    agents.append(BollingerAgent())
    agents.append(SMAAgent())
    agents.append(MeanReversionAgent())
    agents.append(RSIAgent())
    # RL — chargement seulement si le checkpoint existe
    dqn_p = MODELS / "dqn_agent.pt"
    if dqn_p.exists():
        agents.append(DQNRLAgent(dqn_p))
    a2c_p = MODELS / "a2c_actor.pth"
    if a2c_p.exists():
        agents.append(A2CRLAgent(a2c_p))
    ppo_p = MODELS / "ppo_policy_FINAL.pth"
    if ppo_p.exists():
        agents.append(PPORLAgent(ppo_p))
    dreamer_p = MODELS / "dreamer_agent.pth"
    if dreamer_p.exists():
        agents.append(DreamerRLAgent(dreamer_p))
    return agents


def main():
    print("=" * 70)
    print("  ÉVALUATION HORS-ÉCHANTILLON — SOLUSDT mars 2026")
    print("=" * 70)

    bars = build_volume_bars(CSV_TEST, volume_threshold=1000)
    env = TradingEnv(bars)

    agents = build_agents()
    print(f"[setup] {len(agents)} agents, {N_EPISODES} épisodes / agent, "
          f"{MAX_STEPS} steps / épisode")
    print()

    summary_rows = []
    for ag in agents:
        print(f"[run] {ag.name} ...", end=" ", flush=True)
        rows = evaluate(env, ag)
        s = summarise(ag.name, rows)
        summary_rows.append(s)
        print(f"PnL_mean={s['pnl_mean']:+.3f}  win={s['win_rate_%']:.1f}%  "
              f"trades≈{s['trades_mean']:.1f}")

    df = pd.DataFrame(summary_rows)
    df = df.sort_values("pnl_mean", ascending=False).reset_index(drop=True)

    out_csv = ROOT / "eval" / "oos_results.csv"
    df.to_csv(out_csv, index=False, float_format="%.4f")

    print("\n" + "=" * 70)
    print("  RÉSULTATS (triés par PnL moyen décroissant)")
    print("=" * 70)
    cols = ["agent", "pnl_mean", "pnl_median", "pnl_std",
            "win_rate_%", "sharpe", "trades_mean",
            "%_buy", "%_sell", "%_hold"]
    print(df[cols].to_string(index=False, float_format=lambda x: f"{x:>8.3f}"))
    print(f"\n[saved] {out_csv}")

    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt

        fig, axes = plt.subplots(1, 2, figsize=(14, 6))
        names = df["agent"].tolist()
        colors = ["#4aa3ff" if n in {"DQN", "A2C", "PPO", "Dreamer"}
                  else "#66bb6a" for n in names]

        ax = axes[0]
        ax.barh(names, df["pnl_mean"], color=colors, edgecolor="black")
        ax.axvline(0, color="black", linestyle="--", alpha=0.6)
        ax.set_title(f"PnL moyen ({N_EPISODES} épisodes hors-échantillon)")
        ax.set_xlabel("PnL moyen (USD/SOL)")
        ax.invert_yaxis()
        ax.grid(alpha=0.3, axis="x")

        ax = axes[1]
        ax.barh(names, df["win_rate_%"], color=colors, edgecolor="black")
        ax.axvline(50, color="black", linestyle="--", alpha=0.6)
        ax.set_title("Taux de victoire (% épisodes profitables)")
        ax.set_xlabel("Win rate (%)")
        ax.invert_yaxis()
        ax.grid(alpha=0.3, axis="x")

        fig.suptitle(
            "SOLUSDT mars 2026 (jamais vu à l'entraînement) — "
            "RL en bleu, règles en vert",
            fontsize=12, fontweight="bold",
        )
        plt.tight_layout()
        out_png = ROOT / "eval" / "oos_results.png"
        plt.savefig(out_png, dpi=140, bbox_inches="tight")
        print(f"[saved] {out_png}")
    except Exception as e:
        print(f"[warn] plot non généré: {e}")


if __name__ == "__main__":
    main()
