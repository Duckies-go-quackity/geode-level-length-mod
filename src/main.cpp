#include <Geode/modify/LevelInfoLayer.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>
#include <Geode/binding/GameObject.hpp>
#include <Geode/binding/GJDifficultySprite.hpp>
#include <Geode/loader/Mod.hpp>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <cstdio>
#include <string>

using namespace geode::prelude;

static constexpr int TIME_LABEL_TAG = 918273;
static constexpr int COUNT_LABEL_TAG = 918274;
static constexpr int HIDE_TOGGLE_TAG = 918275;

static float speedForState(int state) {
    switch (state) {
        case 0: return 251.16f; // Slow
        case 1: return 311.58f; // Normal
        case 2: return 387.42f; // Fast
        case 3: return 468.00f; // Faster
        case 4: return 578.09f; // Fastest
        default: return 311.58f;
    }
}

static std::string formatTime(float seconds) {
    int total = static_cast<int>(seconds + 0.5f);
    int mins = total / 60;
    int secs = total % 60;
    char buf[24];
    if (mins > 0) snprintf(buf, sizeof(buf), "%dm %ds", mins, secs);
    else snprintf(buf, sizeof(buf), "%ds", secs);
    return std::string(buf);
}

struct LevelStats {
    float time = 0.f;
    int objectCount = 0;
};

static LevelStats calculateLevelStats(GJGameLevel* level) {
    LevelStats stats;

    auto editor = LevelEditorLayer::create(level, false);
    if (!editor) return stats;
    editor->retain();

    auto objects = editor->m_objects;
    if (!objects) {
        editor->release();
        return stats;
    }

    stats.objectCount = objects->count();
    if (stats.objectCount == 0) {
        editor->release();
        return stats;
    }

    struct SpeedChange { float x; int state; };
    std::vector<SpeedChange> changes;
    changes.push_back({0.f, 1});

    float maxX = 0.f;
    for (unsigned int i = 0; i < objects->count(); i++) {
        auto obj = static_cast<GameObject*>(objects->objectAtIndex(i));
        if (!obj) continue;

        int state = -1;
        switch (obj->m_objectID) {
            case 200: state = 0; break;
            case 201: state = 1; break;
            case 202: state = 2; break;
            case 203: state = 3; break;
            case 1334: state = 4; break;
        }
        if (state != -1) changes.push_back({obj->getPositionX(), state});
        if (obj->getPositionX() > maxX) maxX = obj->getPositionX();
    }

    std::sort(changes.begin(), changes.end(), [](auto& a, auto& b) { return a.x < b.x; });

    float time = 0.f;
    for (size_t i = 0; i < changes.size(); i++) {
        float segStart = changes[i].x;
        float segEnd = (i + 1 < changes.size()) ? changes[i + 1].x : maxX;
        if (segEnd <= segStart) continue;
        time += (segEnd - segStart) / speedForState(changes[i].state);
    }

    stats.time = time;
    editor->release();
    return stats;
}

static std::unordered_map<int, std::string> s_timeCache;
static std::unordered_map<int, int> s_countCache;

static void ensureStatsComputed(GJGameLevel* level) {
    int id = level->m_levelID;
    if (s_timeCache.find(id) != s_timeCache.end()) return;

    std::string timeKey = "time_" + std::to_string(id);
    std::string countKey = "count_" + std::to_string(id);

    std::string persistedTime = Mod::get()->getSavedValue<std::string>(timeKey, "");
    int persistedCount = Mod::get()->getSavedValue<int>(countKey, -1);

    if (!persistedTime.empty() && persistedCount >= 0) {
        s_timeCache[id] = persistedTime;
        s_countCache[id] = persistedCount;
        return;
    }

    if (level->m_levelString.empty()) return;

    auto stats = calculateLevelStats(level);
    std::string formatted = formatTime(stats.time);

    s_timeCache[id] = formatted;
    s_countCache[id] = stats.objectCount;

    Mod::get()->setSavedValue<std::string>(timeKey, formatted);
    Mod::get()->setSavedValue<int>(countKey, stats.objectCount);
}

static std::string getCachedTime(GJGameLevel* level) {
    ensureStatsComputed(level);
    auto it = s_timeCache.find(level->m_levelID);
    return it != s_timeCache.end() ? it->second : "...";
}

static int getCachedCount(GJGameLevel* level) {
    ensureStatsComputed(level);
    auto it = s_countCache.find(level->m_levelID);
    return it != s_countCache.end() ? it->second : 0;
}

static bool isHiddenForLevel(int levelID) {
    return Mod::get()->getSavedValue<bool>("hidden_" + std::to_string(levelID), false);
}

static void setHiddenForLevel(int levelID, bool hidden) {
    Mod::get()->setSavedValue<bool>("hidden_" + std::to_string(levelID), hidden);
}

static CCLabelBMFont* findLengthLabel(CCNode* node) {
    if (!node) return nullptr;
    auto children = node->getChildren();
    if (!children) return nullptr;

    for (unsigned int i = 0; i < children->count(); i++) {
        auto child = static_cast<CCNode*>(children->objectAtIndex(i));
        if (auto label = typeinfo_cast<CCLabelBMFont*>(child)) {
            auto text = label->getString();
            if (text) {
                std::string s(text);
                if (s == "Tiny" || s == "Short" || s == "Medium" || s == "Long" ||
                    s == "XL" || s == "Auto") {
                    return label;
                }
            }
        }
        if (auto found = findLengthLabel(child)) return found;
    }
    return nullptr;
}

static CCLabelBMFont* showLevelTimeLabel(CCLabelBMFont* lengthLabel, const std::string& text) {
    auto parent = lengthLabel->getParent();
    parent->removeChildByTag(TIME_LABEL_TAG, true);

    auto timeLabel = CCLabelBMFont::create(text.c_str(), "bigFont.fnt");
    timeLabel->setScale(0.4f);
    timeLabel->setTag(TIME_LABEL_TAG);

    auto box = lengthLabel->boundingBox();
    float centerX = box.origin.x + box.size.width / 2.f;
    float centerY = box.origin.y + box.size.height / 2.f;
    double offsetX = Mod::get()->getSettingValue<double>("timer-x-axis-offset");
    double offsetY = Mod::get()->getSettingValue<double>("timer-y-axis-offset");
    timeLabel->setPosition({centerX + (float)offsetX, centerY + (float)offsetY});

    parent->addChild(timeLabel, 10);
    return timeLabel;
}

static CCLabelBMFont* showObjectCountLabel(CCLabelBMFont* lengthLabel, int count) {
    if (!lengthLabel || !lengthLabel->getParent()) return nullptr;

    auto parent = lengthLabel->getParent();
    parent->removeChildByTag(COUNT_LABEL_TAG, true);

    std::string text = std::to_string(count) + " objs";
    auto countLabel = CCLabelBMFont::create(text.c_str(), "bigFont.fnt");
    countLabel->setScale(0.35f);
    countLabel->setTag(COUNT_LABEL_TAG);

    auto box = lengthLabel->boundingBox();
    float centerX = box.origin.x + box.size.width / 2.f;
    float topY = box.origin.y + box.size.height;
    
    double offsetX = Mod::get()->getSettingValue<double>("object-count-x-axis-offset");
    double offsetY = Mod::get()->getSettingValue<double>("object-count-y-axis-offset");
    
    // Positioned directly above the length label, with a 10-point padding buffer
    countLabel->setPosition({centerX + (float)offsetX, topY + 10.f + (float)offsetY});

    parent->addChild(countLabel, 10);
    return countLabel;
}

class $modify(MyLevelInfoLayer, LevelInfoLayer) {
    struct Fields {
        CCLabelBMFont* m_lengthTimeLabel = nullptr;
        CCLabelBMFont* m_objectCountLabelPtr = nullptr;
        int m_currentLevelID = 0;
    };

    bool init(GJGameLevel* level, bool challenge) {
        if (!LevelInfoLayer::init(level, challenge)) return false;

        m_fields->m_currentLevelID = level->m_levelID;

        auto lengthLabel = findLengthLabel(this);
        if (!lengthLabel) return true;

        bool hidden = isHiddenForLevel(level->m_levelID);

        if (Mod::get()->getSettingValue<bool>("show-object-count")) {
            if (!level->m_levelString.empty()) {
                m_fields->m_objectCountLabelPtr = showObjectCountLabel(lengthLabel, getCachedCount(level));
            }
        }

        if (!hidden) {
            if (!level->m_levelString.empty()) {
                m_fields->m_lengthTimeLabel = showLevelTimeLabel(lengthLabel, getCachedTime(level));
            } else {
                m_fields->m_lengthTimeLabel = showLevelTimeLabel(lengthLabel, "...");
                if (this->shouldDownloadLevel()) {
                    this->downloadLevel();
                }
            }
        }

        if (Mod::get()->getSettingValue<bool>("show-hide-timer-checkbox")) {
            addHideToggle(lengthLabel, hidden);
        }

        return true;
    }

    // A small, standard GD checkbox placed just below the timer, using
    // real game checkbox sprites -- self-contained on our own page,
    // doesn't touch any shared/global popup.
    void addHideToggle(CCLabelBMFont* lengthLabel, bool hidden) {
        auto onSprite = CCSprite::createWithSpriteFrameName("GJ_checkOn_001.png");
        auto offSprite = CCSprite::createWithSpriteFrameName("GJ_checkOff_001.png");
        if (!onSprite || !offSprite) return;

        onSprite->setScale(0.6f);
        offSprite->setScale(0.6f);

        auto toggle = CCMenuItemToggler::create(
            offSprite, onSprite, this, menu_selector(MyLevelInfoLayer::onHideToggleClicked)
        );
        toggle->toggle(hidden);
        toggle->setTag(HIDE_TOGGLE_TAG);

        auto box = lengthLabel->boundingBox();
        double offsetX = Mod::get()->getSettingValue<double>("checkbox-x-axis-offset");
        double offsetY = Mod::get()->getSettingValue<double>("checkbox-y-axis-offset");
        float posX = box.origin.x + box.size.width / 2.f + (float)offsetX;
        float posY = box.origin.y - 34.f + (float)offsetY;

        auto menu = CCMenu::create();
        menu->setPosition({0.f, 0.f});
        menu->addChild(toggle);
        toggle->setPosition({posX, posY});

        lengthLabel->getParent()->addChild(menu, 15);
    }

    void onHideToggleClicked(CCObject* sender) {
        int id = m_fields->m_currentLevelID;
        if (id == 0) return;

        bool nowHidden = !isHiddenForLevel(id);
        setHiddenForLevel(id, nowHidden);

        if (nowHidden) {
            if (m_fields->m_lengthTimeLabel) {
                m_fields->m_lengthTimeLabel->removeFromParent();
                m_fields->m_lengthTimeLabel = nullptr;
            }
        } else {
            auto lengthLabel = findLengthLabel(this);
            if (lengthLabel && this->m_level && !this->m_level->m_levelString.empty()) {
                m_fields->m_lengthTimeLabel = showLevelTimeLabel(lengthLabel, getCachedTime(this->m_level));
            }
        }
    }

    void levelDownloadFinished(GJGameLevel* level) {
        LevelInfoLayer::levelDownloadFinished(level);

        auto lengthLabel = findLengthLabel(this);

        if (!isHiddenForLevel(level->m_levelID) && m_fields->m_lengthTimeLabel) {
            m_fields->m_lengthTimeLabel->setString(getCachedTime(level).c_str());
        }
        if (Mod::get()->getSettingValue<bool>("show-object-count") && !m_fields->m_objectCountLabelPtr && lengthLabel) {
            m_fields->m_objectCountLabelPtr = showObjectCountLabel(lengthLabel, getCachedCount(level));
        }
    }

    void levelDownloadFailed(int p0) {
        LevelInfoLayer::levelDownloadFailed(p0);

        if (m_fields->m_lengthTimeLabel) {
            m_fields->m_lengthTimeLabel->setString("N/A");
        }
    }
};