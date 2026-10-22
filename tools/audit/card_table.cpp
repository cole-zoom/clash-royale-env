// Card reference table: writes docs/CARDS.md, one row per registered card.
// Every number is read off the live entities the card spawns on an empty
// board, the way sight_audit.cpp reads sight ranges, so the table shows what
// the engine actually builds rather than what the registry source appears to
// say. Read-only: nothing here changes a card.
//
// Regenerate from the repo root:
//   powershell -File tools/audit/build.ps1 card_table
//   tools/audit/bin/card_table.exe docs/CARDS.md

#include "GameManager.h"
#include "CardRegistry.h"
#include "CardStats.h"
#include "CombatEntity.h"
#include "BuildingTargeter.h"
#include "AreaSpell.h"
#include "Troop.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace {

// CardStats::attackCooldown is stored in ticks; 10 ticks = 1 second.
constexpr float TICKS_PER_SECOND = 10.0f;

const char* const NONE = "-";

std::string fixed(float value, int decimals) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(decimals) << value;
    return out.str();
}

// Tiles per second, plus the real game's tier when the speed IS one. The tiers
// come from CardStats.h's SPEED_* constants, converted the way
// CardRegistry::troop() converts them, so this table cannot keep its own copy.
// The match is exact (float noise only): a raw 0.5f literal lands 0.6% from
// Slow, and labelling it Slow would hide that it is off-tier.
std::string speedCell(float tilesPerTick) {
    struct Tier { const char* name; float registrySpeed; };
    const Tier tiers[] = {
        { "Very slow", SPEED_VERY_SLOW }, { "Slow", SPEED_SLOW },
        { "Medium", SPEED_MEDIUM }, { "Fast", SPEED_FAST },
        { "Very fast", SPEED_VERY_FAST },
    };
    float perSecond = tilesPerTick * TICKS_PER_SECOND;
    std::string cell = fixed(perSecond, 2);
    for (const auto& tier : tiers) {
        float tierPerSecond = tier.registrySpeed * MOVEMENT_SPEED_SCALE * TICKS_PER_SECOND;
        if (std::fabs(perSecond - tierPerSecond) <= 1e-4f * tierPerSecond) {
            return cell + " (" + tier.name + ")";
        }
    }
    return cell;
}

struct Row {
    int id;
    std::string name, cost, type, units, hp, damage, hitSpeed, range, speed, targets, special;
};

// Bodies of one kind a card put down. A compound card spawns a second kind
// beside its main unit, under its own name (Goblin Giant's Spear Goblins) or
// under the card's (Ram Rider's rider, Rascals' Girls).
struct BodyGroup {
    const CombatEntity* first;
    int count;
};

Row describeBodies(Row row, const std::vector<const CombatEntity*>& bodies) {
    std::vector<BodyGroup> groups;
    for (const auto* body : bodies) {
        auto same = std::find_if(groups.begin(), groups.end(), [&](const BodyGroup& g) {
            return g.first->name == body->name && g.first->hp == body->hp
                && g.first->getBaseDamage() == body->getBaseDamage();
        });
        if (same == groups.end()) groups.push_back({ body, 1 });
        else same->count++;
    }
    if (groups.empty()) return row;

    const CombatEntity* main = groups.front().first;
    int mainCount = groups.front().count, sameNameCount = 0;
    std::string otherKinds;
    for (size_t i = 1; i < groups.size(); ++i) {
        if (groups[i].first->name == main->name) sameNameCount += groups[i].count;
        else otherKinds += " + " + std::to_string(groups[i].count) + " " + groups[i].first->name;
    }
    row.units = (sameNameCount > 0)
        ? std::to_string(mainCount + sameNameCount) + " (" + std::to_string(mainCount)
            + " + " + std::to_string(sameNameCount) + ")"
        : std::to_string(mainCount);
    row.units += otherKinds;

    row.hp = std::to_string(main->hp);
    if (main->shieldHp > 0) row.hp += " + " + std::to_string(main->shieldHp) + " shield";

    int damage = main->getBaseDamage();
    if (damage > 0) {
        if (main->rampFullTick > 0) {
            // Inferno: the damage climbs while it stays on one target.
            float top = (main->rampStage4Tick > 0) ? std::max(1.0f, main->rampStage4Fraction) : 1.0f;
            row.damage = std::to_string(static_cast<int>(damage * main->rampStartFraction))
                + " to " + std::to_string(static_cast<int>(damage * top));
        } else {
            row.damage = std::to_string(damage);
        }
        row.hitSpeed = fixed(main->getAttackCooldown() / TICKS_PER_SECOND, 1) + " s";
        row.range = fixed(main->getAttackRange(), 1);
        if (dynamic_cast<const BuildingTargeter*>(main)) row.targets = "Buildings";
        else row.targets = main->targetsAir ? "Air and ground" : "Ground";
    }
    if (const auto* troop = dynamic_cast<const Troop*>(main)) {
        row.speed = speedCell(troop->getSpeed());
        if (main->isFlying) row.type = "Troop, flying";
    }
    return row;
}

Row describeSpell(Row row, const AreaSpell& spell) {
    int hits = spell.getTotalHits();
    if (spell.hasTieredDamage()) {
        row.damage = std::to_string(spell.getTierSingleDamage()) + " / "
            + std::to_string(spell.getTierFewDamage()) + " / "
            + std::to_string(spell.getTierManyDamage());
    } else if (spell.getDamage() > 0) {
        row.damage = std::to_string(spell.getDamage());
        if (hits > 1) row.damage += " x " + std::to_string(hits);
    }
    if (hits > 1 && spell.getTickInterval() > 0) {
        row.hitSpeed = "every " + fixed(spell.getTickInterval() / TICKS_PER_SECOND, 1) + " s";
    }
    if (spell.isRolling()) {
        row.range = fixed(spell.rollWidth, 1) + " wide, rolls " + fixed(spell.rollRange, 1);
    } else if (spell.getRadius() > 0.0f) {
        row.range = "radius " + fixed(spell.getRadius(), 1);
    }
    if (spell.isAllyBuff()) row.targets = "Own troops (buff)";
    else if (spell.isAllyClone()) row.targets = "Own troops (clone)";
    else if (spell.getDamage() > 0 || spell.hasTieredDamage() || spell.hasOnHit()) {
        row.targets = spell.isGroundOnly() ? "Ground" : "Air and ground";
    }
    return row;
}

Row describe(int id, const CardDefinition& def, bool evolved,
             const std::map<std::string, int>& baseIdByName) {
    float wholeCost = std::round(def.cost);
    Row row{ id, def.name, fixed(def.cost, def.cost == wholeCost ? 0 : 1),
             def.isSpell ? "Spell" : def.isBuilding ? "Building" : "Troop",
             NONE, NONE, NONE, NONE, NONE, NONE, NONE, "" };

    if (evolved) {
        auto base = baseIdByName.find(def.name);
        row.special = (base != baseIdByName.end())
            ? "Evolution of " + std::to_string(base->second) : "Evolution";
    } else if (def.isChampion) {
        row.special = "Champion";
    } else if (def.isHero) {
        row.special = "Hero";
    }

    Board board;
    const auto& spawn = evolved ? def.spawnEvolvedEntity : def.spawnEntity;
    spawn(9.0f, 5.0f, 0, board);
    board.commitPendingEntities(0);

    std::vector<const CombatEntity*> bodies;
    const AreaSpell* spell = nullptr;
    for (const auto& e : board.getEntities()) {
        if (const auto* body = dynamic_cast<const CombatEntity*>(e.get())) bodies.push_back(body);
        else if (const auto* s = dynamic_cast<const AreaSpell*>(e.get())) { if (!spell) spell = s; }
    }
    // A troop's deploy burst (Electro Wizard's zap) is an AreaSpell too, so a
    // spell is read only for spell cards.
    if (def.isSpell) return spell ? describeSpell(row, *spell) : row;
    return describeBodies(row, bodies);
}

void writeTable(std::ostream& out, const std::vector<Row>& rows) {
    out << "| ID | Card | Cost | Type | Units | HP | Damage | Hit speed | Range | Speed (tiles/s) | Targets | Special |\n";
    out << "|---:|---|---:|---|---|---:|---:|---:|---|---|---|---|\n";
    for (const auto& r : rows) {
        out << "| " << r.id << " | " << r.name << " | " << r.cost << " | " << r.type
            << " | " << r.units << " | " << r.hp << " | " << r.damage << " | " << r.hitSpeed
            << " | " << r.range << " | " << r.speed << " | " << r.targets
            << " | " << r.special << " |\n";
    }
}

} // namespace

int main(int argc, char** argv) {
    auto& reg = CardRegistry::getInstance();
    std::vector<std::pair<int, const CardDefinition*>> all;
    for (const auto& [id, def] : reg.getAllCards()) all.emplace_back(id, &def);
    std::sort(all.begin(), all.end(), [](auto& a, auto& b) { return a.first < b.first; });

    // Evolutions reuse the base card's name verbatim, so a name is mapped to
    // the lowest non-Evolution id that carries it.
    std::map<std::string, int> baseIdByName;
    for (const auto& [id, def] : all) {
        if (!def->isEvolution) baseIdByName.emplace(def->name, id);
    }

    std::vector<Row> cards, evolutions;
    for (const auto& [id, def] : all) {
        if (def->isEvolution) evolutions.push_back(describe(id, *def, true, baseIdByName));
        else cards.push_back(describe(id, *def, false, baseIdByName));
    }

    std::ofstream file;
    if (argc > 1) {
        // Binary, so the file gets \n line endings on Windows too.
        file.open(argv[1], std::ios::binary);
        if (!file) { std::cerr << "cannot write " << argv[1] << "\n"; return 1; }
    }
    std::ostream& out = (argc > 1) ? static_cast<std::ostream&>(file) : std::cout;

    out << "# Card reference\n\n"
        << "<!-- Generated by tools/audit/card_table.cpp. Do not edit by hand. -->\n\n"
        << "Every card the engine registers: " << cards.size() << " cards and "
        << evolutions.size() << " Evolutions. The numbers are read from the engine\n"
        << "itself, by spawning each card on an empty board, so they are the values a\n"
        << "match is played with.\n\n"
        << "Regenerate it after changing a card, from the repository root:\n\n"
        << "```powershell\n"
        << "powershell -File tools/audit/build.ps1 card_table\n"
        << "tools/audit/bin/card_table.exe docs/CARDS.md\n"
        << "```\n\n"
        << "Without MSVC, under WSL: `bash tools/audit/build.sh card_table && "
        << "tools/audit/bin/card_table docs/CARDS.md`.\n\n"
        << "## How to read it\n\n"
        << "- **HP and damage** are the real game's level-11 values, for the card's main\n"
        << "  unit. A shield is listed separately. An Inferno's damage climbs while it\n"
        << "  stays on one target, so it shows the first and the last step. Void's three\n"
        << "  numbers are its damage against one, a few and many targets.\n"
        << "- **Units** is how many bodies one card places. A second kind of body is\n"
        << "  listed after a plus sign (Goblin Giant carries Spear Goblins). When both\n"
        << "  kinds carry the card's name, the split is in brackets: Ram Rider's 2 (1 + 1)\n"
        << "  is the ram and its rider.\n"
        << "- **Hit speed** is seconds between attacks (the engine counts 10 ticks per\n"
        << "  second). For a spell that hits more than once, it is the time between hits,\n"
        << "  and the damage column shows damage per hit times the number of hits.\n"
        << "- **Range** is in tiles. For a spell it is the radius, or the width and\n"
        << "  rolling distance of The Log and Barbarian Barrel.\n"
        << "- **Speed** is tiles per second. The real game's speed tier is named only\n"
        << "  when the speed is exactly on it, so a speed with no tier is off-tier.\n"
        << "- **Spawner buildings** (huts, Tombstone, Goblin Drill) show no damage: they\n"
        << "  fight through the troops they spawn.\n"
        << "- **Targets** is what the card attacks. \"Buildings\" means it walks past\n"
        << "  troops to hit buildings.\n"
        << "- **Evolution rows show the evolved form.** An Evolution keeps the base card's\n"
        << "  name, so use the ID to tell them apart (\"Evolution of 1\" is Archers).\n\n"
        << "Found a number that does not match the real game? Please open a\n"
        << "[card accuracy report](https://github.com/itzik123/ClashRoyaleAi/issues/new?template=card_accuracy.yml)\n"
        << "with a source for the correct value.\n\n"
        << "## Cards\n\n";
    writeTable(out, cards);
    out << "\n## Evolutions\n\n";
    writeTable(out, evolutions);

    std::cerr << "wrote " << cards.size() << " cards and " << evolutions.size()
              << " Evolutions\n";
    return 0;
}
