"""
rzf_throttling.py
=================
Computes the RZF throttling number for bidirectional paths and cycles.

    th_rzf(G) = min over all S of (|S| + ept_rzf(G, S))

Uses exact Markov chain / dynamic programming approach from:
  Geneson, Hicks, Lichtenberg, Moon, Robles (2026)

Run in Jupyter or VS Code. Requires:
    pip install networkx numpy scipy pandas matplotlib
"""

import numpy as np
import networkx as nx
import pandas as pd
import matplotlib.pyplot as plt
import itertools
import math
from scipy.sparse import lil_matrix, csr_matrix
from collections import defaultdict

# ─────────────────────────────────────────────
#  SECTION 1: Core binary / state utilities
# ─────────────────────────────────────────────

def d2b(i, n):
    """Integer i → zero-padded binary string of length n."""
    s = bin(i)[2:]
    return s.zfill(n)

def b2d(s):
    """Binary string → integer."""
    return int(s, 2)

def transition_possible(i, j, n):
    """True iff state j is reachable from state i in one step (bits only go 0→1)."""
    bi, bj = d2b(i, n), d2b(j, n)
    return all(bi[k] == '1' or bj[k] == '0' for k in range(n))

def differing_digits(i, j, n):
    """Indices where i has 0 and j has 1 (vertices that turn blue)."""
    bi, bj = d2b(i, n), d2b(j, n)
    return [n - k - 1 for k in range(n) if bi[k] == '0' and bj[k] == '1']

def same_zeros(i, j, n):
    """Indices that are 0 in both i and j (white vertices that stay white)."""
    bi, bj = d2b(i, n), d2b(j, n)
    return [n - k - 1 for k in range(n) if bi[k] == '0' and bj[k] == '0']

def blue_indices(i, n):
    """List of vertex indices that are blue in state i."""
    b = d2b(i, n)
    return [n - k - 1 for k in range(n) if b[k] == '1']

def white_indices(i, n):
    """List of vertex indices that are white in state i."""
    b = d2b(i, n)
    return [n - k - 1 for k in range(n) if b[k] == '0']

# ─────────────────────────────────────────────
#  SECTION 2: RZF probability rule
# ─────────────────────────────────────────────

def forced_prob_directed(G, state, node, n):
    """
    Probability that `node` turns blue in next round given current `state`,
    under the RZF rule (weighted or unweighted).
    """
    predecessors = list(G.predecessors(node))
    if len(predecessors) == 0:
        return 0.0

    blue = set(blue_indices(state, n))

    # Check if graph is weighted
    is_weighted = all('weight' in G[u][node] for u in predecessors)

    if not is_weighted:
        blue_preds = sum(1 for u in predecessors if u in blue)
        return blue_preds / len(predecessors)
    else:
        total_w = sum(G[u][node]['weight'] for u in predecessors)
        if total_w == 0:
            return 0.0
        blue_w = sum(G[u][node]['weight'] for u in predecessors if u in blue)
        return blue_w / total_w

def reachable_next_states(state, G, n):
    """
    All states reachable from `state` in one RZF step:
    only white vertices that have at least one blue in-neighbor are eligible.
    """
    blue = set(blue_indices(state, n))
    white = white_indices(state, n)

    eligible = [
        v for v in white
        if any(u in blue for u in G.predecessors(v))
    ]

    next_states = []
    for r in range(len(eligible) + 1):
        for subset in itertools.combinations(eligible, r):
            new_state = state
            for v in subset:
                new_state += 2 ** v
            next_states.append(new_state)

    return sorted(set(next_states))

# ─────────────────────────────────────────────
#  SECTION 3: Transition matrix (sparse)
# ─────────────────────────────────────────────

def build_transition_matrix(G, n):
    """
    Build the 2^n × 2^n sparse transition matrix for RZF on directed graph G.
    """
    size = 2 ** n
    tm = lil_matrix((size, size))

    for i in range(size):
        for j in reachable_next_states(i, G, n):
            prob = 1.0
            for k in differing_digits(i, j, n):
                prob *= forced_prob_directed(G, i, k, n)
            for m in same_zeros(i, j, n):
                prob *= (1 - forced_prob_directed(G, i, m, n))
            tm[i, j] += prob  # accumulate in case of duplicates

    # absorbing state
    tm[size - 1, size - 1] = 1.0
    return tm.tocsr()

# ─────────────────────────────────────────────
#  SECTION 4: EPT solver (dynamic programming)
# ─────────────────────────────────────────────

def solve_ept(tm, n):
    """
    Compute expected propagation time from every state using backward DP.
    Returns array of length 2^n.
    solutions[i] = EPT starting from state i.
    solutions[0] = inf (empty starting set).
    solutions[2^n - 1] = 0 (already all blue).
    """
    size = tm.shape[0]
    solutions = np.full(size, np.inf)
    solutions[size - 1] = 0.0

    for i in range(size - 2, 0, -1):
        diag = tm[i, i]
        coeff = 1.0 - diag

        if coeff < 1e-15:
            solutions[i] = np.inf
            continue

        row = tm.getrow(i)
        total = 0.0
        for idx, val in zip(row.indices, row.data):
            if idx != i and val > 0:
                total += val * (solutions[idx] + 1)

        total += (1.0 - coeff)  # self-loop contribution
        solutions[i] = total / coeff

    solutions[0] = np.inf
    return solutions

# ─────────────────────────────────────────────
#  SECTION 5: Throttling computation
# ─────────────────────────────────────────────

def compute_throttling(G, n, verbose=False):
    """
    Compute th_rzf(G) = min over all S of (|S| + ept_rzf(G, S)).

    Returns:
        th        : the throttling number
        opt_sets  : list of optimal initial sets (as lists of vertex indices)
        opt_size  : size of optimal initial set
        opt_ept   : EPT of optimal initial set
        all_results : list of (set_size, set_indices, ept, throttle_value)
                      for every nonempty S with finite EPT
    """
    tm = build_transition_matrix(G, n)
    solutions = solve_ept(tm, n)

    all_results = []
    best_throttle = np.inf
    opt_sets = []
    opt_size = None
    opt_ept = None

    for state in range(1, 2 ** n):
        ept = solutions[state]
        if np.isinf(ept):
            continue
        blues = blue_indices(state, n)
        s = len(blues)
        throttle = s + ept

        all_results.append((s, blues, ept, throttle))

        if verbose:
            print(f"  S={blues}, |S|={s}, EPT={ept:.4f}, "
                  f"|S|+EPT={throttle:.4f}")

        if throttle < best_throttle - 1e-10:
            best_throttle = throttle
            opt_sets = [blues]
            opt_size = s
            opt_ept = ept
        elif abs(throttle - best_throttle) < 1e-10:
            opt_sets.append(blues)

    return best_throttle, opt_sets, opt_size, opt_ept, all_results

# ─────────────────────────────────────────────
#  SECTION 6: Graph constructors
# ─────────────────────────────────────────────

def make_bidirectional_path(n):
    """Bidirectional path P_n: edges i↔i+1 for i=0..n-2."""
    G = nx.DiGraph()
    G.add_nodes_from(range(n))
    for i in range(n - 1):
        G.add_edge(i, i + 1)
        G.add_edge(i + 1, i)
    return G

def make_bidirectional_cycle(n):
    """Bidirectional cycle C_n: edges i↔i+1 (mod n)."""
    G = nx.DiGraph()
    G.add_nodes_from(range(n))
    for i in range(n):
        G.add_edge(i, (i + 1) % n)
        G.add_edge((i + 1) % n, i)
    return G

# ─────────────────────────────────────────────
#  SECTION 7: Run experiments
# ─────────────────────────────────────────────

def run_path_experiment(max_n=14):
    """
    Compute th_rzf(P_n) for n = 2 to max_n.
    Also records the optimal set size and EPT separately.
    """
    print("=" * 65)
    print("RZF THROTTLING: BIDIRECTIONAL PATHS")
    print("=" * 65)
    print(f"{'n':>4} | {'th_rzf':>10} | {'|S*|':>6} | {'EPT':>10} | {'Optimal S'}")
    print("-" * 65)

    records = []
    for n in range(2, max_n + 1):
        G = make_bidirectional_path(n)
        th, opt_sets, opt_size, opt_ept, _ = compute_throttling(G, n)
        # show first optimal set only
        s_str = str(opt_sets[0]) if opt_sets else "—"
        print(f"{n:>4} | {th:>10.4f} | {opt_size:>6} | {opt_ept:>10.4f} | {s_str}")
        records.append({
            'n': n,
            'th_rzf': round(th, 6),
            'opt_set_size': opt_size,
            'opt_ept': round(opt_ept, 6),
            'num_opt_sets': len(opt_sets),
            'one_opt_set': opt_sets[0] if opt_sets else None
        })

    return pd.DataFrame(records)


def run_cycle_experiment(max_n=12):
    """
    Compute th_rzf(C_n) for n = 3 to max_n.
    """
    print("\n" + "=" * 65)
    print("RZF THROTTLING: BIDIRECTIONAL CYCLES")
    print("=" * 65)
    print(f"{'n':>4} | {'th_rzf':>10} | {'|S*|':>6} | {'EPT':>10} | {'Optimal S'}")
    print("-" * 65)

    records = []
    for n in range(3, max_n + 1):
        G = make_bidirectional_cycle(n)
        th, opt_sets, opt_size, opt_ept, _ = compute_throttling(G, n)
        s_str = str(opt_sets[0]) if opt_sets else "—"
        print(f"{n:>4} | {th:>10.4f} | {opt_size:>6} | {opt_ept:>10.4f} | {s_str}")
        records.append({
            'n': n,
            'th_rzf': round(th, 6),
            'opt_set_size': opt_size,
            'opt_ept': round(opt_ept, 6),
            'num_opt_sets': len(opt_sets),
            'one_opt_set': opt_sets[0] if opt_sets else None
        })

    return pd.DataFrame(records)


def plot_results(df_path, df_cycle):
    """
    Plot th_rzf vs n for paths and cycles side by side.
    """
    fig, axes = plt.subplots(1, 2, figsize=(13, 5))

    # --- Paths ---
    ax = axes[0]
    ax.plot(df_path['n'], df_path['th_rzf'], 'o-', color='steelblue',
            linewidth=2, markersize=6, label=r'$\mathrm{th}_{\mathrm{rzf}}(P_n)$')
    ax.plot(df_path['n'], df_path['opt_set_size'], 's--', color='tomato',
            linewidth=1.5, markersize=5, label=r'$|S^*|$')
    ax.plot(df_path['n'], df_path['opt_ept'], '^--', color='seagreen',
            linewidth=1.5, markersize=5, label=r'$\mathrm{ept}_{\mathrm{rzf}}(P_n, S^*)$')
    ax.set_xlabel('$n$', fontsize=13)
    ax.set_ylabel('Value', fontsize=13)
    ax.set_title(r'RZF Throttling on Bidirectional Paths $P_n$', fontsize=13)
    ax.legend(fontsize=10)
    ax.grid(True, alpha=0.3)

    # --- Cycles ---
    ax = axes[1]
    ax.plot(df_cycle['n'], df_cycle['th_rzf'], 'o-', color='steelblue',
            linewidth=2, markersize=6, label=r'$\mathrm{th}_{\mathrm{rzf}}(C_n)$')
    ax.plot(df_cycle['n'], df_cycle['opt_set_size'], 's--', color='tomato',
            linewidth=1.5, markersize=5, label=r'$|S^*|$')
    ax.plot(df_cycle['n'], df_cycle['opt_ept'], '^--', color='seagreen',
            linewidth=1.5, markersize=5, label=r'$\mathrm{ept}_{\mathrm{rzf}}(C_n, S^*)$')
    ax.set_xlabel('$n$', fontsize=13)
    ax.set_ylabel('Value', fontsize=13)
    ax.set_title(r'RZF Throttling on Bidirectional Cycles $C_n$', fontsize=13)
    ax.legend(fontsize=10)
    ax.grid(True, alpha=0.3)

    plt.tight_layout()
    plt.savefig('rzf_throttling_results.png', dpi=150, bbox_inches='tight')
    plt.show()
    print("\nPlot saved to rzf_throttling_results.png")


# ─────────────────────────────────────────────
#  SECTION 8: Main
# ─────────────────────────────────────────────

if __name__ == "__main__":

    # Paths up to n=12, cycles up to n=12
    # Increase carefully — cost is O(2^n) so n=14 is the practical limit
    MAX_N = 19

    df_path  = run_path_experiment(max_n=MAX_N)
    df_cycle = run_cycle_experiment(max_n=MAX_N)

    # Save raw tables
    df_path.to_csv('rzf_throttling_paths.csv', index=False)
    df_cycle.to_csv('rzf_throttling_cycles.csv', index=False)
    print("\nCSV tables saved to rzf_throttling_paths.csv and rzf_throttling_cycles.csv")

    # Plot
    plot_results(df_path, df_cycle)

    # Print summary
    print("\n--- PATH SUMMARY ---")
    print(df_path.to_string(index=False))
    print("\n--- CYCLE SUMMARY ---")
    print(df_cycle.to_string(index=False))

