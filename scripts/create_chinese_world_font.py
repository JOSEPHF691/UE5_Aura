"""Create a compact offline Chinese font for UTextRenderComponent world labels.

Canvas HUD uses UE's runtime Slate font with CJK fallback. TextRender needs an
offline atlas, so import only the characters actually used by world labels.
"""

import os

import unreal


font_path = "/Game/Aura/UI/Fonts/F_AuraWorldChinese"
assets = unreal.EditorAssetLibrary
phrases = (
    "大魔法师商人锻造大师生命法力检查点已激活宝箱开启队伍战利品"
    "封印剩余名守卫打开奖励仪式完成召唤魔王需要三处请先击败"
    "伏击精英宝藏恢复首领房间清理突破外围萨满恶魔领主增益"
    "在中生存穿越险境侧翼个人专属拾取"
    "药水奥术法杖护甲先知头饰疾风之靴洗点石矿"
    "织焰唤雷堡垒法袍护法披肩灰烬冠冕壁兜帽迅捷坚守"
    "洞穴蘑菇哥布林枪兵食尸鬼"
    "宝箱尚未解锁队伍奖励宝箱封印宝箱剩余守卫"
    "仪式已完成封印已激活召唤魔王需要三处封印"
    "激活路线封印清理房间突破外围守卫"
    "精英增益守卫在伏击中生存穿越险境侧翼"
    "普通优质精良史诗传说"
)
chars = "".join(sorted(set(phrases)))
existing = assets.load_asset(font_path) if assets.does_asset_exist(font_path) else None
if existing and set(existing.get_editor_property("import_options").get_editor_property("chars")) == set(chars):
    unreal.log_warning("AURA_CHINESE_WORLD_FONT_EXISTS")
else:
    if existing:
        raise RuntimeError(
            "Chinese world font already exists with a different glyph set; "
            "reimport the asset explicitly before rerunning this script"
        )
    factory = unreal.TrueTypeFontFactory()
    options = factory.get_editor_property("import_options")
    data = options.get_editor_property("data")
    data.set_editor_property(
        "font_name", os.path.join(
            unreal.Paths.engine_content_dir(), "Slate", "Fonts", "DroidSansFallback.ttf"
        )
    )
    data.set_editor_property("height", 32.0)
    data.set_editor_property("chars", chars)
    data.set_editor_property("include_ascii_range", True)
    data.set_editor_property("use_distance_field_alpha", True)
    data.set_editor_property("distance_field_scale_factor", 4)
    data.set_editor_property("texture_page_width", 512)
    data.set_editor_property("texture_page_max_height", 512)
    options.set_editor_property("data", data)
    factory.set_editor_property("import_options", options)
    font = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        "F_AuraWorldChinese", "/Game/Aura/UI/Fonts", unreal.Font, factory
    )
    if not font:
        raise RuntimeError("Could not create Chinese offline font")
    if not assets.save_loaded_asset(font, only_if_is_dirty=False):
        raise RuntimeError("Could not save Chinese offline font")
    unreal.log_warning(
        f"AURA_CHINESE_WORLD_FONT_SUCCESS chars={len(chars)} "
        f"cache={font.get_editor_property('font_cache_type')}"
    )
