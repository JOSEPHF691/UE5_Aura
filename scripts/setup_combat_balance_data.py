import unreal

ASSET_PATH = "/Game/Aura/Data/DA_CombatBalance"
ASSET_DIR = "/Game/Aura/Data"
ASSET_NAME = "DA_CombatBalance"

asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
editor_assets = unreal.EditorAssetLibrary


def make_struct(struct_type, **values):
    result = struct_type()
    for property_name, value in values.items():
        result.set_editor_property(property_name, value)
    return result


if not editor_assets.does_directory_exist(ASSET_DIR):
    editor_assets.make_directory(ASSET_DIR)

balance = editor_assets.load_asset(ASSET_PATH) if editor_assets.does_asset_exist(ASSET_PATH) else None
if not balance:
    factory = unreal.DataAssetFactory()
    factory.set_editor_property("data_asset_class", unreal.AuraCombatBalanceData)
    balance = asset_tools.create_asset(
        ASSET_NAME, ASSET_DIR, unreal.AuraCombatBalanceData, factory
    )
if not balance:
    raise RuntimeError(f"Failed to create {ASSET_PATH}")

balance.set_editor_property("balance_version", unreal.Name("DemoBalanced_v2_0_CampEconomy"))
balance.set_editor_property(
    "player",
    make_struct(
        unreal.AuraPlayerCombatBalance,
        max_health=100.0,
        max_mana=50.0,
        strength=10.0,
        intelligence=10.0,
        armor=0.0,
        combat_exit_delay=3.0,
    ),
)
balance.set_editor_property(
    "progression",
    make_struct(
        unreal.AuraPlayerProgressionBalance,
        max_level=10,
        base_experience_to_next_level=60,
        experience_growth_per_level=40,
        experience_to_next_level=[60, 100, 140, 180, 210, 230, 250, 270, 290],
        max_health_per_level=10.0,
        max_mana_per_level=3.0,
        strength_per_level=1.0,
        intelligence_per_level=1.0,
        armor_per_level=1.0,
        ability_damage_bonus_per_level=0.04,
    ),
)
balance.set_editor_property(
    "run_upgrades",
    make_struct(
        unreal.AuraRunUpgradeBalance,
        max_rank_per_upgrade=3,
        fire_bolt_damage_bonus_per_rank=0.15,
        arcane_shards_damage_bonus_per_rank=0.20,
        arcane_shards_radius_bonus_per_rank=0.10,
        shock_damage_bonus_per_rank=0.20,
        shock_stun_bonus_per_rank=0.25,
        max_health_per_rank=20.0,
        max_mana_per_rank=10.0,
        universal_damage_bonus_per_rank=0.08,
    ),
)
balance.set_editor_property(
    "fire_bolt",
    make_struct(
        unreal.AuraFireBoltBalance,
        damage=15.0,
        mana_cost=0.0,
        cooldown=0.75,
        release_delay=0.28,
    ),
)
balance.set_editor_property(
    "arcane_shards",
    make_struct(
        unreal.AuraArcaneShardsBalance,
        damage=50.0,
        mana_cost=18.0,
        cooldown=5.0,
        cast_delay=0.42,
        cast_range=900.0,
        effect_radius=300.0,
    ),
)
balance.set_editor_property(
    "shock",
    make_struct(
        unreal.AuraShockBalance,
        damage=40.0,
        mana_cost=14.0,
        cooldown=6.0,
        impact_delay=0.16,
        cast_range=950.0,
        aim_assist_radius=105.0,
        stun_duration=1.75,
        effect_lifetime=0.45,
    ),
)
balance.set_editor_property(
    "combo",
    make_struct(
        unreal.AuraComboBalance,
        mark_duration=3.0,
        ember_burst_radius=220.0,
        ember_burst_damage_multiplier=0.40,
        fracture_chain_radius=300.0,
        fracture_chain_damage_multiplier=0.35,
        fracture_chain_max_targets=2,
        overload_radius=180.0,
        overload_damage_multiplier=0.65,
    ),
)
balance.set_editor_property(
    "skill_ranks",
    make_struct(
        unreal.AuraSkillRankBalance,
        max_rank=4,
        fire_bolt_damage_bonus=0.10,
        arcane_shards_radius_bonus=0.10,
        shock_damage_bonus=0.10,
        fire_bolt_extra_pierces=1,
        arcane_shards_field_duration=2.0,
        arcane_shards_field_radius=260.0,
        arcane_shards_slow_fraction=0.30,
    ),
)
balance.set_editor_property(
    "growth_economy",
    make_struct(
        unreal.AuraGrowthEconomyBalance,
        common_kill_souls=12,
        elite_kill_souls=38,
        boss_kill_souls=125,
        elite_room_catch_up_experience=10,
        seal_room_clear_souls=20,
    ),
)
balance.set_editor_property(
    "camp_economy",
    make_struct(
        unreal.AuraCampEconomyBalance,
        potion_soul_price=18,
        common_equipment_soul_price=45,
        quality_equipment_soul_price=80,
        respec_stone_soul_price=100,
        respec_stone_purchase_limit=1,
        sell_fraction=0.25,
        forge_one_soul_cost=20,
        forge_two_soul_cost=40,
        forge_three_soul_cost=70,
        forge_one_material_cost=1,
        forge_two_material_cost=2,
        forge_three_material_cost=3,
        weapon_base_stat_bonus_per_level=0.05,
    ),
)
balance.set_editor_property(
    "melee_enemy",
    make_struct(
        unreal.AuraMeleeEnemyBalance,
        max_health=100.0,
        walk_speed=250.0,
        kill_experience_reward=25,
        damage=15.0,
        impact_time=0.88,
        hit_range=205.0,
        attack_interval=1.35,
        ghoul_frenzy_chance=0.30,
        ghoul_frenzy_hit_damage=9.0,
        ghoul_frenzy_first_impact_time=0.58,
        ghoul_frenzy_second_impact_delay=0.52,
        ghoul_frenzy_attack_interval=1.70,
        goblin_jab_damage=12.0,
        goblin_jab_impact_time=0.54,
        goblin_jab_range=215.0,
        goblin_jab_attack_interval=1.15,
        goblin_jab_step_distance=145.0,
        goblin_lunge_chance=0.35,
        goblin_lunge_damage=20.0,
        goblin_lunge_windup=0.82,
        goblin_lunge_engage_range=350.0,
        goblin_lunge_hit_range=225.0,
        goblin_lunge_travel_distance=150.0,
        goblin_lunge_warning_half_width=70.0,
        goblin_lunge_warning_length=375.0,
        goblin_lunge_attack_interval=1.75,
        shroom_bash_damage=11.0,
        shroom_bash_impact_time=0.62,
        shroom_bash_range=185.0,
        shroom_bash_attack_interval=1.25,
        shroom_spore_chance=0.28,
        shroom_spore_damage=22.0,
        shroom_spore_windup=1.10,
        shroom_spore_radius=285.0,
        shroom_spore_attack_interval=2.10,
        spore_exposure_duration=2.0,
        spore_exposure_slow_fraction=0.20,
        spore_detonation_damage_multiplier=1.25,
        max_normal_attacks_before_special=4,
    ),
)
balance.set_editor_property(
    "shaman",
    make_struct(
        unreal.AuraShamanBalance,
        max_health=200.0,
        walk_speed=250.0,
        kill_experience_reward=75,
        damage=20.0,
        windup_time=0.8,
        cast_range=1000.0,
        cooldown=3.0,
        retreat_distance=450.0,
        preferred_distance=700.0,
        attack_range=1000.0,
        volley_chance=0.30,
        max_normal_casts_before_volley=4,
        volley_projectile_count=3,
        volley_spread_degrees=18.0,
        volley_damage_multiplier=0.60,
        volley_windup_bonus=0.25,
    ),
)
balance.set_editor_property(
    "boss",
    make_struct(
        unreal.AuraBossBalance,
        max_health=450.0,
        kill_experience_reward=250,
        phase_two_health_threshold=0.5,
        phase_transition_duration=1.35,
        phase_one_walk_speed=245.0,
        phase_two_walk_speed=310.0,
        melee_range=285.0,
        tail_spin_radius=380.0,
        throw_range=1150.0,
        throw_warning_radius=115.0,
        claw_half_angle_degrees=52.0,
        phase_one_claw_damage=18.0,
        phase_two_claw_damage=22.0,
        tail_spin_damage=28.0,
        throw_damage=24.0,
        charge_damage=24.0,
        charge_distance=900.0,
        charge_half_width=120.0,
        tail_spin_chance=0.30,
        charge_chance=0.38,
        summon_chance=0.22,
        max_actions_without_claw=3,
        max_close_actions_without_tail=3,
        max_actions_without_summon=5,
        max_far_actions_without_throw=2,
        claw_impact_time=0.72,
        tail_spin_windup=0.95,
        throw_windup=0.82,
        charge_windup=0.92,
        summon_windup=1.15,
        summon_cooldown=9.0,
        summon_radius=360.0,
        summon_count=2,
        max_active_summons=4,
        recovery_time=0.34,
        phase_one_attack_interval=1.45,
        phase_two_attack_interval=1.08,
    ),
)
balance.set_editor_property(
    "recovery",
    make_struct(
        unreal.AuraWorldRecoveryBalance,
        health_pickup_restore=35.0,
        mana_pickup_restore=20.0,
        training_pickup_respawn_delay=5.0,
        treasure_health_restore=35.0,
        treasure_mana_restore=20.0,
    ),
)
balance.set_editor_property(
    "ai",
    make_struct(
        unreal.AuraAICombatBalance,
        detection_radius=1200.0,
        lose_target_radius=1550.0,
        leash_radius=1850.0,
        melee_attack_range=190.0,
        home_acceptance_radius=45.0,
        move_request_interval=0.22,
    ),
)
balance.set_editor_property(
    "encounters",
    make_struct(
        unreal.AuraEncounterBalance,
        outer_guard_enemy_count=3,
        elite_shaman_enemy_count=1,
        demon_lord_enemy_count=1,
        optional_branch_enemy_count=5,
    ),
)

unreal.EditorAssetLibrary.save_loaded_asset(balance, only_if_is_dirty=False)

# Keep Blueprint defaults useful in editor previews while runtime authority remains
# with DA_CombatBalance.
enemy_blueprints = [
    ("/Game/Aura/Blueprints/BP_AuraEnemy", 100.0, 25, unreal.AuraEnemyArchetype.GHOUL),
    ("/Game/Aura/Blueprints/Dungeon/BP_AuraGoblinSpear", 100.0, 25, unreal.AuraEnemyArchetype.GOBLIN_SPEARMAN),
    ("/Game/Aura/Blueprints/Dungeon/BP_AuraShroom", 100.0, 25, unreal.AuraEnemyArchetype.CAVE_SHROOM),
    ("/Game/Aura/Blueprints/Dungeon/BP_AuraEliteShaman", 200.0, 75, unreal.AuraEnemyArchetype.SHAMAN_ELITE),
    ("/Game/Aura/Blueprints/Dungeon/BP_AuraBossDemon", 450.0, 250, unreal.AuraEnemyArchetype.DEMON_LORD),
]
for blueprint_path, health, experience_reward, archetype in enemy_blueprints:
    blueprint = editor_assets.load_asset(blueprint_path)
    if not blueprint:
        continue
    cdo = unreal.get_default_object(blueprint.generated_class())
    cdo.set_editor_property("starting_health", health)
    cdo.set_editor_property("kill_experience_reward", experience_reward)
    cdo.set_editor_property("enemy_archetype", archetype)
    unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
    editor_assets.save_loaded_asset(blueprint, only_if_is_dirty=False)

# Give the Demon Lord real Blueprint minion classes and dedicated presentation
# sequences. Runtime combat stays native/server-authoritative, while these
# references keep spawned minions and the two new actions visually complete.
boss_blueprint = editor_assets.load_asset("/Game/Aura/Blueprints/Dungeon/BP_AuraBossDemon")
ghoul_blueprint = editor_assets.load_asset("/Game/Aura/Blueprints/BP_AuraEnemy")
goblin_blueprint = editor_assets.load_asset("/Game/Aura/Blueprints/Dungeon/BP_AuraGoblinSpear")
shroom_blueprint = editor_assets.load_asset("/Game/Aura/Blueprints/Dungeon/BP_AuraShroom")
if boss_blueprint and ghoul_blueprint and goblin_blueprint and shroom_blueprint:
    boss_cdo = unreal.get_default_object(boss_blueprint.generated_class())
    boss_cdo.set_editor_property(
        "boss_summon_classes",
        [
            ghoul_blueprint.generated_class(),
            goblin_blueprint.generated_class(),
            shroom_blueprint.generated_class(),
        ],
    )
    boss_cdo.set_editor_property(
        "boss_summon_animation",
        editor_assets.load_asset("/Game/Assets/Enemies/Demon/Animations/Demon_Stun"),
    )
    boss_cdo.set_editor_property(
        "boss_charge_animation",
        editor_assets.load_asset("/Game/Assets/Enemies/Demon/Animations/Demon_Attack_R"),
    )
    unreal.BlueprintEditorLibrary.compile_blueprint(boss_blueprint)
    editor_assets.save_loaded_asset(boss_blueprint, only_if_is_dirty=False)

# Bind the three mandatory dungeon encounters to their Data Asset count profiles.
dungeon_map_path = "/Game/Map/DungeonDemo"
if editor_assets.does_asset_exist(dungeon_map_path):
    world = unreal.EditorLoadingAndSavingUtils.load_map(dungeon_map_path)
    actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    encounter_profiles = {
        "DungeonEncounter_OuterGuard": unreal.AuraEncounterBalanceProfile.OUTER_GUARD,
        "DungeonEncounter_EliteShaman": unreal.AuraEncounterBalanceProfile.ELITE_SHAMAN,
        "DungeonEncounter_DemonLord": unreal.AuraEncounterBalanceProfile.DEMON_LORD,
    }
    for actor in actor_subsystem.get_all_level_actors():
        if actor.get_class().get_name() == "AuraTreasureChest":
            actor.set_editor_property("treasure_id", unreal.Name(actor.get_actor_label()))
            if hasattr(actor, "rerun_construction_scripts"):
                actor.rerun_construction_scripts()
            continue
        profile = encounter_profiles.get(actor.get_actor_label())
        if profile is None:
            continue
        actor.set_editor_property("balance_profile", profile)
        if hasattr(actor, "rerun_construction_scripts"):
            actor.rerun_construction_scripts()
    if not unreal.EditorLoadingAndSavingUtils.save_map(world, dungeon_map_path):
        raise RuntimeError("Failed to save DungeonDemo encounter balance profiles")

unreal.log_warning(
    "AURA_COMBAT_BALANCE_SETUP_SUCCESS: DA_CombatBalance DemoBalanced_v2_0_CampEconomy saved"
)
