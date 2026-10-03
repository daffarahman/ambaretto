#include "police.hpp"
#include "environment.hpp"
#include "pedestrians.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void vehicle_combat(const forza::Environment& map) {
    using namespace forza;
    PhysicsWorld world(map);
    Car car(world);
    Police police(world, map);
    Player player(world, car, map, nullptr, nullptr, nullptr, nullptr, &police);
    const auto advance = [&](int steps, Input input = Input{}) { for (int i = 0; i < steps; ++i) player.step(input, {}); };
    advance(400);
    PoliceUnit* unit = nullptr;
    for (const auto& candidate : police.units()) if (candidate.active) { unit = &const_cast<PoliceUnit&>(candidate); break; }
    require(unit, "no patrol available for vehicle gunfire check");
    unit->car->reset(Vec3(0, map.height(0, 280) + .56f, 280));
    car.reset(Vec3(0, map.height(0, 300) + .56f, 300));
    for (std::size_t i = 0; i < unit->officers.size(); ++i) {
        auto& officer = unit->officers[i];
        const float x = i ? 3.f : -3.f;
        officer.character->reset(Vec3(x, map.height(x, 280) + .08f, 280));
        officer.seated = false; officer.fire_time = 0;
    }
    const auto shots = [&] { return unit->officers[0].shots_fired + unit->officers[1].shots_fired; };
    police.crime(Crime::PoliceVehicleTheft, player.position(), unit->officers[0].character.get());
    world.take_sound_events();
    advance(240);
    const auto sounds = world.take_sound_events();
    require(std::any_of(sounds.begin(), sounds.end(), [](const SoundEvent& sound) { return sound.effect == SoundEffect::Pistol; }),
        "police gunfire did not emit pistol audio");
    require(shots() > 0 && car.health() < 100 && player.character().health() == 100,
        "nearby foot officers did not shoot occupied car or damaged its hidden occupant directly");
    require(car.health() > 90, "initial police volley caused excessive vehicle damage");
    const unsigned stationary_shots = shots();
    advance(180, {1});
    require(car.velocity().Length() > 3 && !unit->officers[0].seated && !unit->officers[1].seated && shots() > stationary_shots,
        "nearby moving car made foot officers stop shooting and immediately reboard");

    car.reset(Vec3(0, map.height(0, 450) + .56f, 450));
    const unsigned close_shots = shots();
    for (auto& officer : unit->officers) officer.fire_time = 0;
    advance(360);
    require(shots() == close_shots && unit->officers[0].seated && unit->officers[1].seated
        && unit->car->velocity().Length() > 2, "distant car was shot from on foot or police failed to resume vehicle pursuit");

    // A close, stopped suspect lets a seated crew park, exit, and fire again.
    unit->car->reset(Vec3(0, map.height(0, 280) + .56f, 280));
    car.reset(Vec3(0, map.height(0, 295) + .56f, 295)); car.repair();
    police.crime(Crime::PoliceVehicleTheft, player.position(), unit->officers[0].character.get());
    advance(300);
    require(!unit->officers[0].seated && !unit->officers[1].seated && shots() > close_shots && car.health() < 100,
        "close pursuit crew failed to exit and shoot the occupied car");

    Car cover_left(world), cover_right(world);
    Car* covers[] = {&cover_left, &cover_right};
    for (std::size_t i = 0; i < unit->officers.size(); ++i) {
        const Vec3 eye = unit->officers[i].character->position() + Vec3(0, 1.5f, 0);
        const Vec3 target = car.position() + car.rotate(Vec3(0, chassis_offset, 0));
        covers[i]->reset((eye + target) / 2 - Vec3(0, chassis_offset, 0));
        unit->officers[i].fire_time = 0;
    }
    const unsigned before_cover = shots();
    const float protected_health = car.health();
    advance(1);
    require(shots() == before_cover && car.health() == protected_health, "police shot an occupied car through solid cover");
    std::cout << "Vehicle combat: nearby gunfire, moving targets, return to pursuit, dismounting and cover passed\n";
}
}
int main() {
    using namespace forza;
    try {
        WantedLevel wanted;
        const Vec3 origin(0, 3.2f, 0);
        wanted.report(Crime::RecklessDriving, origin);
        require(wanted.stars() == 1, "minor crime did not start at one star");
        wanted.report(Crime::Assault, origin);
        require(wanted.stars() == 2, "assault did not escalate the response");
        wanted.report(Crime::OfficerHomicide, origin);
        require(wanted.stars() >= 4, "officer homicide had a minor response");
        for (int i = 0; i < 100; ++i) wanted.report(Crime::OfficerHomicide, origin);
        require(wanted.stars() == 6, "wanted level exceeded or did not reach six stars");
        for (int i = 1; i <= 6; ++i) {
            const auto& response = police_response(i);
            require(response.cars == i && response.speed > 0, "stars did not determine police numbers");
            if (i > 1) require(response.speed > police_response(i - 1).speed
                && response.fire_interval < police_response(i - 1).fire_interval, "aggression did not increase");
            if (i > 2) require(response.pit_interval < police_response(i - 1).pit_interval, "PIT rate did not increase");
        }
        require(police_response(1).pit_interval == 0, "one-star pursuit uses dangerous PIT tactics");
        wanted.step(origin, false, 60);
        require(wanted.stars() == 6 && wanted.searching(), "hiding inside search area cleared wanted level");
        const Vec3 outside = origin + Vec3(wanted.radius() + 50, 0, 0);
        wanted.step(outside, false, wanted.cooldown() / 2);
        require(wanted.stars() == 6 && wanted.escape_progress() > .4f, "escape lacked its outside-area cooldown");
        require((wanted.last_seen() - origin).Length() < .001f, "search area followed an unseen player");
        wanted.step(origin, false, .1f);
        require(wanted.escape_progress() == 0, "returning inside did not cancel escape countdown");
        wanted.step(outside, false, 5); wanted.step(outside, true, .1f);
        require(wanted.escape_progress() == 0 && !wanted.searching(), "police sighting did not restart pursuit");
        wanted.step(outside + Vec3(500, 0, 0), false, wanted.cooldown() + .1f);
        require(wanted.stars() == 0, "escaping did not clear the wanted level");
        std::cout << "Wanted: crime severity, six levels, escalating tactics and escape rules passed\n";

        const Environment map;
        PhysicsWorld world(map);
        Car car(world);
        Pedestrians pedestrians(world, map);
        Police police(world, map, nullptr, &pedestrians);
        Player player(world, car, map, nullptr, nullptr, &pedestrians, nullptr, &police);
        const auto advance = [&](int steps, Input input = Input{}) { for (int i = 0; i < steps; ++i) player.step(input, {}); };
        police.crime(Crime::Gunfire, Vec3(2800, 0, 1800));
        advance(360);
        require(police.wanted().stars() == 0, "unwitnessed isolated gunfire alerted police magically");
        const PoliceUnit* patrol = nullptr;
        for (const auto& unit : police.units()) if (unit.active) { patrol = &unit; break; }
        require(patrol && patrol->car->type() == CarType::Police, "ambient police car did not spawn");
        require(!patrol->officers[0].seated || !patrol->officers[1].seated, "ambient foot officers did not spawn");
        Character* witness = nullptr;
        for (const auto& person : pedestrians.people()) if (person.enabled && !person.character->ragdolling()
            && (person.character->position() - patrol->car->position()).Length() > 150) { witness = person.character.get(); break; }
        require(witness, "no isolated civilian witness for report check");
        police.crime(Crime::Gunfire, witness->position());
        require(police.wanted().stars() == 0, "civilian report skipped the call delay");
        advance(330);
        require(police.wanted().stars() >= 1, "witness did not report the crime");
        bool panicked = false;
        for (const auto& person : pedestrians.people()) panicked |= person.fear_time > 0;
        require(panicked, "civilians did not react to gunfire");
        require(!police.road_path(Vec3(0, 3.2f, 105), Vec3(1260, 3.2f, 240)).empty(), "police cannot navigate the Miami causeway");
        require(!police.road_path(Vec3(-360, 3.2f, 900), Vec3(-2100, 3.2f, 4380)).empty(), "police cannot navigate the Keys bridges");
        std::cout << "Witnesses: isolation, delayed reports, panic, foot patrol and road routing passed\n";

        police.clear(); player.reset();
        for (int i = 0; i < 4; ++i) {
            police.crime(Crime::OfficerHomicide, player.position(), police.units()[0].officers[0].character.get());
            advance(24, {1});
        }
        require(police.wanted().stars() == 6, "serious repeated crimes did not reach six stars");
        // Keep dispatch clear of arrest, PITs and lethal high-speed city crashes.
        for (int i = 0; i < 2400; ++i) {
            advance(1, {.1f});
            if (std::count_if(police.units().begin(), police.units().end(), [](const PoliceUnit& unit) { return unit.active; }) == 6) break;
        }
        int count = 0;
        for (const auto& unit : police.units()) if (unit.active) {
            ++count;
            require(unit.car->simulated() && std::isfinite(unit.car->position().LengthSq()), "dispatch created an invalid/nonphysical car");
            require(!map.submerged(unit.car->position()), "police spawned or drove into water");
        }
        require(count == 6, "six stars did not dispatch six police units");
        std::cout << "Dispatch: six physical police units and safe spawning passed\n";

        player.reset(); advance(120, {1});
        auto& pit_unit = const_cast<PoliceUnit&>(police.units()[0]);
        auto& pit_car = *pit_unit.car;
        pit_car.repair();
        const unsigned attempts = pit_unit.pit_attempts;
        pit_car.reset(car.position() - car.forward() * 6 + car.forward().Cross(Vec3::sAxisY()) * 2.5f);
        // A PIT starts with a mounted crew and a suspect already driving away.
        pit_unit.pit_cooldown = 0;
        for (auto& crew : pit_unit.officers) {
            crew.character->reset(pit_car.position()); crew.character->revive(); crew.character->set_enabled(false); crew.seated = true;
        }
        advance(420, {1});
        require(police.units()[0].pit_attempts > attempts, "eligible six-star police did not attempt a PIT maneuver");
        require(std::abs(car.position().GetX()) > .2f || std::abs(car.forward().GetX()) > .05f,
            "PIT steering did not physically disturb the suspect vehicle");
        std::cout << "PIT: rear-quarter pursuit physically moved the suspect vehicle\n";

        police.clear(); player.reset(); advance(400);
        patrol = nullptr;
        for (const auto& unit : police.units()) if (unit.active) { patrol = &unit; break; }
        require(patrol, "patrol failed to return after wanted reset");
        const auto& officer = patrol->officers[0];
        const Vec3 feet = officer.character->position() + Vec3(0, 0, 7);
        player.respawn_on_foot(Vec3(feet.GetX(), map.surface_height(feet) + .08f, feet.GetZ()));
        police.crime(Crime::VehicleTheft, player.position(), officer.character.get());
        advance(60);
        require(!officer.seated && officer.character->alive(), "close police did not pursue on foot");
        player.reset(); advance(600, {1});
        require(patrol->officers[0].seated && patrol->officers[1].seated, "officers did not reenter their car when player drove away");
        std::cout << "Police transitions: close foot pursuit and vehicle reentry passed\n";

        police.clear(); player.reset(); advance(400);
        patrol = nullptr;
        for (const auto& unit : police.units()) if (unit.active) { patrol = &unit; break; }
        require(patrol && !patrol->officers[0].seated, "no officer available for arrest check");
        const auto cop_feet = patrol->officers[0].character->position();
        const Vec3 nearby = cop_feet + Vec3(0, 0, 4);
        player.respawn_on_foot(Vec3(nearby.GetX(), map.surface_height(nearby) + .08f, nearby.GetZ()));
        police.crime(Crime::VehicleTheft, player.position(), patrol->officers[0].character.get());
        advance(360);
        require(police.arrested() && player.character().alive(), "one-star police failed to make a nonlethal arrest");
        require(!player.can_shoot() && player.interact() == Interaction::Blocked, "arrested player could shoot or enter a car");
        police.clear();
        require(police.wanted().stars() == 0 && !police.arrested(), "respawn did not clear wanted/arrest state");

        player.reset(); advance(400);
        patrol = nullptr;
        for (const auto& unit : police.units()) if (unit.active) { patrol = &unit; break; }
        require(patrol && !patrol->officers[0].seated && !patrol->officers[1].seated, "no empty patrol car for theft check");
        const Vec3 door = patrol->car->position() + patrol->car->rotate(Vec3(-2, 0, .35f));
        player.respawn_on_foot(Vec3(door.GetX(), map.surface_height(door) + .08f, door.GetZ()));
        require(player.entry_car() == patrol->car.get() && player.interact() == Interaction::Entered, "player could not steal an empty police car");
        require(police.wanted().stars() >= 2 && patrol->claimed, "police-car theft did not alert nearby officers");
        const int theft_stars = police.wanted().stars();
        require(player.interact() == Interaction::Exited && player.interact() == Interaction::Entered,
            "stolen police car could not be exited and reentered");
        require(police.wanted().stars() == theft_stars, "reentering an owned car counted as another theft");
        require(player.interact() == Interaction::Exited, "could not exit police car for combat check");

        auto& target = *patrol->officers[0].character;
        const Vec3 head = target.body_parts()[int(BodyPart::Head)].position;
        Vec3 shooter = head;
        bool clear_shot = false;
        for (const auto offset : {Vec3(0, 0, 5), Vec3(0, 0, -5), Vec3(5, 0, 0), Vec3(-5, 0, 0)}) {
            const Vec3 candidate = head + offset;
            if (trace_shot(world, &pedestrians, candidate, -offset, 10, &police).character == &target) {
                shooter = candidate; clear_shot = true; break;
            }
        }
        require(clear_shot, "officer body was missing from shared weapon raycast");
        player.weapons().select(WeaponType::Pistol);
        const auto shot = player.shoot(shooter, head - shooter, true, true, true);
        require(shot.fired && shot.victim == &target && target.health() < 100 && target.ragdolling(), "shooting police did not damage their physical ragdoll");
        require(police.wanted().stars() >= 3, "shooting an officer did not escalate the wanted level");
        const Vec3 ray = (head - shooter).Normalized();
        car.reset((head + shooter) / 2 - Vec3(0, chassis_offset, 0));
        const auto covered = trace_shot(world, &pedestrians, shooter, ray, 10, &police);
        require(covered.character != &target && covered.distance < 4, "bullets hit an officer through a vehicle");
        std::cout << "Combat and theft: officer ragdolls, cover, police-car theft and ownership passed\n";

        car.reset(map.spawn());
        // Use a clear avenue so the earlier theft/cover scene cannot shelter the suspect.
        const Vec3 cop_position(-360, map.height(-360, 250) + .08f, 250);
        target.revive(); target.reset(Vec3(cop_position.GetX(), map.surface_height(cop_position) + .08f, cop_position.GetZ()));
        const Vec3 suspect_position = cop_position + Vec3(0, 0, 8);
        player.respawn_on_foot(Vec3(suspect_position.GetX(), map.surface_height(suspect_position) + .08f, suspect_position.GetZ()));
        player.weapons().select(WeaponType::AK47);
        // Six-star accuracy keeps this hit check independent of random low-star misses.
        for (int i = 0; i < 4; ++i) {
            police.crime(Crime::OfficerHomicide, player.position(), &target);
            advance(24);
        }
        advance(600);
        require(patrol->officers[0].shots_fired > 0 && player.character().health() < 100,
            "armed police did not fire or their shots failed to damage the player");
        std::cout << "Police combat: native aiming and gunfire damaged the suspect\n";
        vehicle_combat(map);
        std::cout << "All police checks passed.\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << "Police check failed: " << error.what() << '\n'; return 1; }
}
