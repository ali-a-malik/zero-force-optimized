# rzf_throttling
Initial findings:
This repository contains the research, code, and write-up for a study of throttling in randomized zero forcing (RZF) on bidirectional paths. Randomized zero forcing is a stochastic graph coloring process introduced by Geneson, Hicks, Lichtenberg, Moon, and Robles (2026) in which a white vertex turns blue with probability equal to the fraction of its incoming neighbors that are already blue. The RZF throttling number minimizes the total cost ∣S∣+eptrzf(G,S)|S| + \mathrm{ept}_{\mathrm{rzf}}(G, S)
∣S∣+eptrzf​(G,S) over all choices of initial blue set SS
S, balancing the upfront cost of the starting set against the expected time for the infection to spread to all vertices. This is the first systematic study of throttling in the RZF setting, directly addressing open problems posed in the original RZF paper. Exact throttling numbers for bidirectional paths are computed for n=2n = 2
n=2 through n=19n = 19
n=19 using a Markov chain dynamic programming approach, and the results reveal a rich structure including exact formulas for small nn
n, phase transitions where clean integer formulas break down, and computational evidence for an asymptotic throttling number of Θ(n)\Theta(\sqrt{n})
Θ(n​) for large nn
n.