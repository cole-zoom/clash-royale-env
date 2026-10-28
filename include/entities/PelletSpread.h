#pragma once

// A fan of straight-line projectiles that collide with what they touch
// (PelletProjectile in Projectile.h): the Hunter's 10 pellets, the
// Firecracker's 5 sparks. count == 0 is "no fan".
//
// The fan is deterministic, `count` shots evenly spaced across fanDegrees and
// centred on the aim line (the client's Scatter = "Line"; its 200 ms
// RandomDelay on the Hunter is not modelled), keeping combat reproducible.
struct PelletSpread {
    int count = 0;
    float fanDegrees = 0.0f;      // total angle, edge shot to edge shot
    float range = 0.0f;           // how far each shot travels before it vanishes
    float collisionRadius = 0.0f; // the shot's own radius (client ProjectileRadius)
    float speed = 0.0f;           // tiles per tick
    bool piercing = false;        // false: gone on its first collision; true: hits each enemy once, flies on
};
