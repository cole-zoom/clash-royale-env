#pragma once
#include "CombatEntity.h"

class Troop : public CombatEntity {
protected:
    float speed;

public:
    bool riverIgnores = false;

    // Base speed for the observation encoder. Not freeze-adjusted: the
    // observation describes the unit, and a freeze shows up as it not moving.
    float getSpeed() const { return speed; }

    Troop(int id, float x, float y, int hp, int team, char symbol,
        float speed, float attackRange, int damage, int attackCooldown)
        : CombatEntity(id, x, y, hp, team, symbol, attackRange, damage, attackCooldown),
        speed(speed) {}

    void setIgnoresRiver(bool ignore) {
        riverIgnores = ignore;
    }

    // Fidelity::clientCollision (separation 0x71fff6058c): a moving troop pushes and is pushed, except while
    // deploying (DISABLE_PHYSICAL during the deploy states: likely), dashing (state 3), winding up a special attack
    // (Fisherman) or jumping the river (state 5: off the ground and not collidable). A troop that cannot move (NO_MOVE)
    // takes no part either.
    bool collidesAsBody(const Board& board) const override {
        if (speed <= 0.0f || deployTicksRemaining > 0 || !isAreaHittable()) return false;
        if (isDashing() || specialLoadLeftMs > 0) return false;
        return !(jumpingRiver || overRiverJump(board));
    }
    bool respectsRiver() const override { return !riverIgnores && !isFlying; }

protected:
    void moveTowards(Board& board, const Vector2D& dest) override {
        Vector2D waypoint = riverIgnores ? dest : board.getNextWaypoint(position, dest);
        float dx = waypoint.x - position.x;
        float dy = waypoint.y - position.y;
        float distToWaypoint = position.distanceTo(waypoint);

        // Must be the same constant getNextWaypoint uses for arrival; two
        // different epsilons produce an absorbing state at the bridge mouths
        // (tests/core/test_board.cpp).
        if (distToWaypoint > Board::WAYPOINT_ARRIVAL_EPS) {
            // The tick-start freeze (frozenThisTick / freezeSlowThisTick), not
            // freezeTicks: update() has already counted the freezes down by
            // now. A movement-only slow (Poison) competes with it; the
            // stronger applies, never both.
            const float freezeFactor = frozenThisTick ? freezeSlowThisTick : 1.0f;
            float currentSpeed = speed * std::min(freezeFactor, moveSlowThisTick);
            // A charge runs faster (Prince, Battle Ram...: 2x in the game).
            if (isCharging()) currentSpeed *= chargeSpeedMultiplier;
            // Land on the waypoint, never past it: an overshoot along a bank
            // line keeps the unit "below" the river, so it is handed the same
            // bridge mouth back and orbits it forever (UPSTREAM_REQUESTS.md
            // item 31; tests/core/test_board.cpp, [orbit]).
            float step = std::min(currentSpeed, distToWaypoint);
            Vector2D newPos;
            newPos.x = position.x + (dx / distToWaypoint) * step;
            newPos.y = position.y + (dy / distToWaypoint) * step;

            // Fidelity::clientCollision: a JumpEnabled troop whose step reaches a river cell jumps it (state 5, jump
            // start 0x71fff61220) at its JumpSpeed instead of walking, no charge bonus, until it lands on a dry cell.
            if (Fidelity::clientCollision && jumpSpeed > 0.0f && riverIgnores && !isFlying) {
                const bool jump = board.isRiverCellAt(position.x, position.y) || board.isRiverCellAt(newPos.x, newPos.y);
                if (jump) {
                    step = std::min(jumpSpeed * std::min(freezeFactor, moveSlowThisTick), distToWaypoint);
                    newPos.x = position.x + (dx / distToWaypoint) * step;
                    newPos.y = position.y + (dy / distToWaypoint) * step;
                }
                jumpingRiver = jump && board.isRiverCellAt(newPos.x, newPos.y);
            }
            // ...and a troop a body push left in a river cell walks out under the same per-cell clamp
            // (Board::clientRiverMove), not through the river.
            if (Fidelity::clientCollision && riverPushed && respectsRiver() && board.isRiverCellAt(position.x, position.y)) {
                newPos = board.clientRiverMove(position, newPos, false);
            }

            // Flying troops pass over building footprints.
            position = isFlying ? newPos : board.resolvePositionAgainstBuildings(newPos, id, bodyRadiusOf(*this));
        }
    }

    void clampPosition(Board& board) override {
        // Fidelity::clientCollision: a centre a body push carried into a river cell stays there (bounds only); the
        // per-cell clamp and the nudge take it back to the bank. Anything else in the river is snapped as before.
        if (Fidelity::clientCollision && riverPushed) {
            if (board.isRiverCellAt(position.x, position.y)) {
                position = board.clampToBoard(position, true);
                return;
            }
            riverPushed = false;
        }
        position = board.clampToBoard(position, riverIgnores);
    }

private:
    // A JumpEnabled troop with its centre over a river cell is mid-jump (Fidelity::clientCollision).
    bool overRiverJump(const Board& board) const {
        return jumpSpeed > 0.0f && riverIgnores && !isFlying && board.isRiverCellAt(position.x, position.y);
    }
};
