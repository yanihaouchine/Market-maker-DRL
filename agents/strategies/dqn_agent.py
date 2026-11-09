"""
Deep Q-Network (DQN) Agent for Cryptocurrency Trading

This module implements a DQN agent for making trading decisions (BUY, SELL, HOLD)
based on cryptocurrency market data. It includes:
- Neural network for Q-value approximation
- Experience replay buffer for decorrelated learning
- Target network for stable training
- Epsilon-greedy exploration strategy
"""

import numpy as np
import tensorflow as tf
from tensorflow import keras
from keras import layers
from collections import deque
from typing import Tuple, Optional

class DQNNetwork(keras.Model):
    """MLP estimating the Q-value of each action."""
    def __init__(self, state_dim: int, num_actions: int = 3, hidden_sizes: tuple = (256, 256, 128, 64), **kwargs):
        super().__init__(**kwargs)
        # Store constructor args so the model can be serialized
        self.state_dim = int(state_dim)
        self.num_actions = int(num_actions)
        self.hidden_sizes = tuple(int(h) for h in hidden_sizes)

        self.hidden_layers = []
        for i, h in enumerate(self.hidden_sizes):
            layer = layers.Dense(h, activation='relu', name=f'dense_{i+1}')
            setattr(self, f'dense_{i+1}', layer)
            self.hidden_layers.append(layer)

        self.output_layer = layers.Dense(self.num_actions, name='q_output')

    def call(self, state: tf.Tensor, training: bool = False) -> tf.Tensor:
        x = state
        for layer in self.hidden_layers:
            x = layer(x, training=training)
        return self.output_layer(x)

    def get_config(self):
        config = super().get_config()
        config.update({
            'state_dim': self.state_dim,
            'num_actions': self.num_actions,
            'hidden_sizes': list(self.hidden_sizes),
        })
        return config

    @classmethod
    def from_config(cls, config):
        if 'hidden_sizes' in config and isinstance(config['hidden_sizes'], list):
            config['hidden_sizes'] = tuple(config['hidden_sizes'])
        return cls(**config)


class ReplayBuffer:
    """Fixed-size buffer of transitions, sampled uniformly."""
    def __init__(self, max_size: int = 50000, state_dim: int = 121):
        self.max_size = max_size
        self.state_dim = state_dim
        self.buffer = deque(maxlen=max_size)

    def add(self, state: np.ndarray, action: int, reward: float,
            next_state: np.ndarray, done: bool) -> None:
        self.buffer.append((state, action, reward, next_state, done))

    def sample(self, batch_size: int) -> Tuple[np.ndarray, np.ndarray, np.ndarray,
                                                 np.ndarray, np.ndarray]:
        indices = np.random.choice(len(self.buffer), batch_size, replace=False)
        states, actions, rewards, next_states, dones = [], [], [], [], []

        for idx in indices:
            state, action, reward, next_state, done = self.buffer[idx]
            states.append(state)
            actions.append(action)
            rewards.append(reward)
            next_states.append(next_state)
            dones.append(done)

        return (np.array(states, dtype=np.float32),
                np.array(actions, dtype=np.int32),
                np.array(rewards, dtype=np.float32),
                np.array(next_states, dtype=np.float32),
                np.array(dones, dtype=np.float32))

    def __len__(self) -> int:
        return len(self.buffer)

    def is_ready(self, batch_size: int) -> bool:
        return len(self.buffer) >= batch_size


class DQNAgent:
    """DQN with target network, experience replay and epsilon-greedy exploration."""

    def __init__(self, state_dim: int, num_actions: int = 3,
                 learning_rate: float = 1e-3, gamma: float = 0.99,
                 epsilon: float = 1.0, epsilon_min: float = 0.01,
                 epsilon_decay: float = 0.995, target_update_freq: int = 1000,
                 replay_buffer_size: int = 50000, batch_size: int = 32):
        self.state_dim = state_dim
        self.num_actions = num_actions
        self.gamma = gamma
        self.epsilon = epsilon
        self.epsilon_min = epsilon_min
        self.epsilon_decay = epsilon_decay
        self.target_update_freq = target_update_freq
        self.batch_size = batch_size

        hidden = (512, 512, 256, 128)
        self.q_network = DQNNetwork(state_dim, num_actions, hidden_sizes=hidden)
        self.target_network = DQNNetwork(state_dim, num_actions, hidden_sizes=hidden)

        self.target_network.set_weights(self.q_network.get_weights())

        self.optimizer = keras.optimizers.Adam(learning_rate=learning_rate)
        self.loss_fn = keras.losses.MeanSquaredError()

        self.replay_buffer = ReplayBuffer(max_size=replay_buffer_size, state_dim=state_dim)

        self.training_steps = 0
        self.losses = []
        self.q_values_mean = []

    def select_action(self, state: np.ndarray, training: bool = True) -> int:
        """
        Select an action using epsilon-greedy policy.

        With probability epsilon: select a random action (exploration)
        Otherwise: select the action with highest Q-value (exploitation)
        """
        if training and np.random.random() < self.epsilon:
            return np.random.randint(0, self.num_actions)
        else:
            state_t = tf.convert_to_tensor(np.expand_dims(state, 0), dtype=tf.float32)
            q_values = self.q_network(state_t, training=False)
            return np.argmax(q_values.numpy()[0])

    def remember(self, state: np.ndarray, action: int, reward: float,
                 next_state: np.ndarray, done: bool) -> None:
        self.replay_buffer.add(state, action, reward, next_state, done)

    def replay(self) -> Optional[float]:
        """
        Train on a batch of transitions from the replay buffer.

        This performs one gradient descent step using the Bellman equation:
        Q(s,a) = r + gamma * max_a' Q(s', a')

        The target network (slowly-updated copy) is used to compute target Q-values,
        which provides stable learning targets.
        """
        if not self.replay_buffer.is_ready(self.batch_size):
            return None

        states, actions, rewards, next_states, dones = self.replay_buffer.sample(self.batch_size)

        with tf.GradientTape() as tape:
            q_values = self.q_network(states, training=True)

            next_q_values = self.target_network(next_states, training=False)
            max_next_q_values = tf.reduce_max(next_q_values, axis=1)

            # Compute target: r + gamma * max_a' Q(s', a')
            # (if done=1, target is just the reward)
            targets = rewards + self.gamma * max_next_q_values * (1.0 - dones)

            target_q_values = q_values.numpy()
            for i in range(self.batch_size):
                target_q_values[i, actions[i]] = targets[i].numpy()
            target_q_values = tf.convert_to_tensor(target_q_values, dtype=tf.float32)

            loss = self.loss_fn(target_q_values, q_values)

        grads = tape.gradient(loss, self.q_network.trainable_variables)
        self.optimizer.apply_gradients(zip(grads, self.q_network.trainable_variables))

        self.losses.append(float(loss))
        mean_q = tf.reduce_mean(q_values).numpy()
        self.q_values_mean.append(float(mean_q))

        self.training_steps += 1

        if self.training_steps % self.target_update_freq == 0:
            self.target_network.set_weights(self.q_network.get_weights())

        return float(loss)

    def decay_epsilon(self) -> None:
        """
        Decay the exploration rate (epsilon) after each episode.

        This transitions the agent from exploration to exploitation over time.
        """
        self.epsilon = max(self.epsilon_min, self.epsilon * self.epsilon_decay)

    def save(self, policy_path: str) -> None:
        self.q_network.save(policy_path)
        print(f"Model saved to {policy_path}")

    def load(self, policy_path: str) -> None:
        self.q_network = keras.models.load_model(policy_path)
        self.target_network.set_weights(self.q_network.get_weights())
        print(f"Model loaded from {policy_path}")

    def get_epsilon(self) -> float:
        return self.epsilon
