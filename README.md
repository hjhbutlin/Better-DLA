<h1>Diffusion limited aggregation (to an end!)</h1>

<h2>Motivation</h2>
This was a piece of university coursework that was meant to have little emphasis on the coding, with code for a simple 2D simulation given to the cohort.
Not only was the code restricted to 2D, but it was wildly inefficient. On top of this,
I wanted to try to replicate realities with a slightly more DLA algorithm.

<h2>Aggregates</h2>

DLA is a subject of interest in many areas of science, and has been around for nearly 4 decades. It involves spawning a particle far away from a cluster of particles, and letting it
execute a random walk until it hits the aggreagte, at which point it sticks, and the process repeats with this new cluster. Below are diagrams to aid this explanation:
