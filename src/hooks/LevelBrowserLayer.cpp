#include <Geode/Geode.hpp>
#include <Geode/modify/LevelBrowserLayer.hpp>
#include <Geode/modify/CCLayer.hpp>

#include "../utils.hpp"
#include "../ui/DoubleArrow.h"
#include "../ui/FolderButton.h"
#include "../layers/LevelBrowserEndLayer.h"
#include "../layers/LevelFiltering/LevelSearchViewLayer.h"
#include "../layers/LevelFiltering/ProfileSearchOptions.h"
#include "../layers/FoundListsPopup.h"

#include "../managers/BetterInfoCache.h"
#include "../utils/LevelUtils.h"

#include <algorithm>
#include <vector>

using namespace geode::prelude;

static const std::unordered_map<SearchType, const char*> s_labels = {
    { SearchType::HallOfFame, "hallOfFameLabel_001.png"_spr },
};

static const std::unordered_map<SearchType, std::pair<const char*, const char*>> s_infoTexts = {
    // currently empty, we are caught up to latest release
};

/**
 * BetterInfo+ cycle states
*/
static constexpr int BIP_VIS_COUNT = 5;
static const char* s_bipVisLabels[BIP_VIS_COUNT] = { "ALL", "PUB", "UNL", "U-O", "F-O" };
static const char* s_bipVisDescs[BIP_VIS_COUNT] = {
    "Showing all levels",
    "Showing public levels only",
    "Showing unlisted levels (incl. friends only)",
    "Showing unlisted levels (excl. friends only)",
    "Showing friends only levels"
};

static constexpr int BIP_SORT_COUNT = 7;
static const char* s_bipSortLabels[BIP_SORT_COUNT] = { "OFF", "DL-", "DL+", "LK-", "LK+", "NEW", "OLD" };
static const char* s_bipSortDescs[BIP_SORT_COUNT] = {
    "Page sort: off",
    "Page sort: most downloads first",
    "Page sort: least downloads first",
    "Page sort: most likes first",
    "Page sort: least likes first",
    "Page sort: newest first",
    "Page sort: oldest first"
};

static constexpr int BIP_COMP_COUNT = 3;
static const char* s_bipCompLabels[BIP_COMP_COUNT] = { "OFF", "HIDE", "ONLY" };
static const char* s_bipCompDescs[BIP_COMP_COUNT] = {
    "Completed filter: off",
    "Hiding completed levels",
    "Showing only completed levels"
};

class BI_DLL 
$modify(BILevelBrowserLayer, LevelBrowserLayer) {
    struct Fields {
        CCMenuItemSpriteExtra* m_biLastPageBtn = nullptr;
        DoubleArrow* m_biLastPageBtnArrow = nullptr;

        // BetterInfo+ additions
        int m_bipVisibilityMode = 0;
        int m_bipSortMode = 0;
        int m_bipCompletedMode = 0;
        CCMenuItemSpriteExtra* m_bipVisibilityBtn = nullptr;
        CCMenuItemSpriteExtra* m_bipSortBtn = nullptr;
        CCMenuItemSpriteExtra* m_bipCompletedBtn = nullptr;
        Ref<cocos2d::CCArray> m_bipCurrentLevels;
    };

    static void onModify(auto& self) {
        (void) self.setHookPriority("LevelBrowserLayer::onGoToPage", Priority::VeryLatePre);
    }

    /*
     * Callbacks
     */
    void onLevelBrowserFilter(CCObject* sender){
        if(BetterInfo::isLocal(this->m_searchObject)) ProfileSearchOptions::create(this, "user_search")->show();
        else {
            auto layer = LevelSearchViewLayer::scene(this->m_searchObject);
            auto transitionFade = CCTransitionFade::create(0.5, layer);
            CCDirector::sharedDirector()->pushScene(transitionFade);
        }
    }    

    void onLevelBrowserRandom(CCObject* sender){
        if(this->m_searchObject == nullptr) return;
        int pageMax = (this->m_itemCount - 1) / BetterInfo::levelsPerPage(this->m_searchObject);

        int pageToLoad = BetterInfo::randomNumber(0, pageMax);

        this->m_searchObject->m_page = pageToLoad;
        this->loadPage(this->m_searchObject);
    }

    void onLevelBrowserFirst(CCObject* sender){
        this->m_searchObject->m_page = 0;
        this->loadPage(this->m_searchObject);
    }

    void onLevelBrowserLast(CCObject* sender){
        if(shouldSearchForLastPage()) {
            LevelBrowserEndLayer::create(this, nullptr)->show();
            return;
        }

        this->m_searchObject->m_page = (this->m_itemCount - 1) / BetterInfo::levelsPerPage(this->m_searchObject);
        this->loadPage(this->m_searchObject);
    }

    void onLevelBrowserStar(CCObject* sender){
        if(this->m_searchObject == nullptr) return;

        this->m_searchObject->m_starFilter = !(this->m_searchObject->m_starFilter);
        this->loadPage(this->m_searchObject);

        auto button = static_cast<CCMenuItemSpriteExtra*>(sender);
        if(this->m_searchObject->m_starFilter) button->setColor({255, 255, 255});
        else button->setColor({125,125,125});
    }

    void onLevelBrowserHide(CCObject* sender){
        if(m_searchObject == nullptr || (m_searchObject->m_searchType != SearchType::UsersLevels && !std::string_view(m_searchObject->m_searchQuery).starts_with("&type=6"))) return;

        bool isCurrentlyFiltered = std::string_view(m_searchObject->m_searchQuery).starts_with("&type=");

        if(!isCurrentlyFiltered) {
            m_searchObject->m_searchQuery = fmt::format("&type={}&str={}", (int) m_searchObject->m_searchType, m_searchObject->m_searchQuery);
        } else {
            m_searchObject->m_searchType = SearchType::UsersLevels;
            auto pos = std::string_view(m_searchObject->m_searchQuery).find('=');
            if(pos != std::string_view::npos) {
                pos = std::string_view(m_searchObject->m_searchQuery).find('=', pos + 1);
                if(pos != std::string_view::npos) {
                    m_searchObject->m_searchQuery = std::string(std::string_view(m_searchObject->m_searchQuery).substr(pos + 1));
                }
            }
        }
        this->loadPage(m_searchObject);

        auto button = static_cast<CCMenuItemSpriteExtra*>(sender);
        if(isCurrentlyFiltered) button->setColor({255, 255, 255});
        else button->setColor({125,125,125});

        Notification::create(fmt::format("Unlisted levels {}", isCurrentlyFiltered ? "shown" : "hidden"), CCSprite::createWithSpriteFrameName("hideBtn_001.png"))->show();
    }

    /**
     * BetterInfo+ callbacks
    */
    void onBipVisibility(CCObject* sender){
        if(!bipIsLevelBrowser()) return;

        m_fields->m_bipVisibilityMode = (m_fields->m_bipVisibilityMode + 1) % BIP_VIS_COUNT;
        bipUpdateButtons();
        BetterInfo::showUnimportantNotification(s_bipVisDescs[m_fields->m_bipVisibilityMode], NotificationIcon::None, 1.5f);
        this->loadPage(m_searchObject);
    }

    void onBipSort(CCObject* sender){
        if(!bipIsLevelBrowser()) return;

        m_fields->m_bipSortMode = (m_fields->m_bipSortMode + 1) % BIP_SORT_COUNT;
        bipUpdateButtons();
        BetterInfo::showUnimportantNotification(s_bipSortDescs[m_fields->m_bipSortMode], NotificationIcon::None, 1.5f);
        this->loadPage(m_searchObject);
    }

    void onBipCompleted(CCObject* sender){
        if(!bipIsLevelBrowser()) return;

        m_fields->m_bipCompletedMode = (m_fields->m_bipCompletedMode + 1) % BIP_COMP_COUNT;
        bipUpdateButtons();
        BetterInfo::showUnimportantNotification(s_bipCompDescs[m_fields->m_bipCompletedMode], NotificationIcon::None, 1.5f);
        this->loadPage(m_searchObject);
    }

    void onBipCopyIds(CCObject* sender){
        auto levels = m_fields->m_bipCurrentLevels;
        std::string ids;

        if(levels) {
            for(unsigned int i = 0; i < levels->count(); i++) {
                auto level = typeinfo_cast<GJGameLevel*>(levels->objectAtIndex(i));
                if(!level) continue;

                if(!ids.empty()) ids += ",";
                ids += fmt::format("{}", level->m_levelID.value());
            }
        }

        if(ids.empty()) {
            BetterInfo::showUnimportantNotification("No level IDs to copy on this page", NotificationIcon::Error, 1.5f);
            return;
        }

        BetterInfo::copyToClipboard(ids.c_str());
    }

    /**
     * Helpers
    */
    void refreshButtonVisibility() {
        if(auto searchMenu = getChildByID("search-menu")) {
            if(auto firstButton = searchMenu->getChildByID("first-button"_spr)) {
                firstButton->setVisible(m_searchObject->m_page > 0);
            }
        }

        if(auto pageMenu = getChildByID("page-menu")) {
            if(auto lastButton = pageMenu->getChildByID("last-button"_spr)) {
                lastButton->setVisible(m_rightArrow->isVisible());
            }
            if(auto randomButton = pageMenu->getChildByID("random-button"_spr)) {
                randomButton->setVisible(this->m_itemCount > BetterInfo::levelsPerPage(this->m_searchObject));
            }
        }

        if(auto lastBtn = m_fields->m_biLastPageBtnArrow) {
            lastBtn->usePopupTexture(shouldSearchForLastPage());
        }
    }

    bool canBeLocalFiltered() {
        return BetterInfo::isLocal(m_searchObject) && m_searchObject->isLevelSearchObject() && m_searchObject->m_searchType != SearchType::MyLevels && m_searchObject->m_searchMode == 0;
    }

    void showFilteredText() {
        if(canBeLocalFiltered() && BetterInfo::isSavedFiltered() && this->m_countText) this->m_countText->setString((std::string("(Filtered) ") + this->m_countText->getString()).c_str());
    }

    /**
     * BetterInfo+ helpers
    */
    static CCSprite* bipFrameSprite(const char* name, const char* fallback) {
        if(CCSpriteFrameCache::sharedSpriteFrameCache()->spriteFrameByName(name)) return CCSprite::createWithSpriteFrameName(name);

        return CCSprite::createWithSpriteFrameName(fallback);
    }

    static void bipAttachModeLabel(CCSprite* sprite, const char* text) {
        auto label = CCLabelBMFont::create(text, "bigFont.fnt");
        label->setScale(0.25f);
        label->setPosition({sprite->getContentSize().width / 2, 2.f});
        label->setZOrder(1);
        label->setID("mode-label"_spr);
        sprite->addChild(label);
    }

    bool bipIsLevelBrowser() {
        return m_searchObject != nullptr && m_searchObject->m_searchMode == 0 && BetterInfo::isLevelSearchObject(m_searchObject);
    }

    bool bipAnyProcessingActive() {
        return m_fields->m_bipVisibilityMode != 0 || m_fields->m_bipSortMode != 0 || m_fields->m_bipCompletedMode != 0;
    }

    bool bipLevelCompleted(GJGameLevel* level) {
        if(level->m_normalPercent == 100) return true;

        auto saved = LevelUtils::getLevelFromSaved(level->m_levelID.value());
        return saved != nullptr && saved->m_normalPercent == 100;
    }

    bool bipMatchesVisibility(GJGameLevel* level) {
        switch(m_fields->m_bipVisibilityMode) {
            case 1: return !level->m_unlisted;
            case 2: return level->m_unlisted;
            case 3: return level->m_unlisted && !level->m_friendsOnly;
            case 4: return level->m_friendsOnly;
            default: return true;
        }
    }

    bool bipMatchesCompleted(GJGameLevel* level) {
        switch(m_fields->m_bipCompletedMode) {
            case 1: return !bipLevelCompleted(level);
            case 2: return bipLevelCompleted(level);
            default: return true;
        }
    }

    cocos2d::CCArray* bipProcessLevels(cocos2d::CCArray* levels) {
        if(levels == nullptr || !bipIsLevelBrowser() || !bipAnyProcessingActive()) return levels;

        std::vector<GJGameLevel*> filtered;
        filtered.reserve(levels->count());

        for(unsigned int i = 0; i < levels->count(); i++) {
            auto level = typeinfo_cast<GJGameLevel*>(levels->objectAtIndex(i));
            if(level == nullptr) return levels; //unexpected content, do not touch anything

            if(!bipMatchesVisibility(level) || !bipMatchesCompleted(level)) continue;

            filtered.push_back(level);
        }

        const int sortMode = m_fields->m_bipSortMode;
        if(sortMode != 0) {
            std::stable_sort(filtered.begin(), filtered.end(), [sortMode](GJGameLevel* a, GJGameLevel* b) {
                switch(sortMode) {
                    case 1: return a->m_downloads > b->m_downloads;
                    case 2: return a->m_downloads < b->m_downloads;
                    case 3: return a->m_likes > b->m_likes;
                    case 4: return a->m_likes < b->m_likes;
                    case 5: return a->m_levelID.value() > b->m_levelID.value();
                    case 6: return a->m_levelID.value() < b->m_levelID.value();
                    default: return false;
                }
            });
        }

        auto result = cocos2d::CCArray::create();
        for(auto level : filtered) result->addObject(level);
        return result;
    }

    void bipUpdateButton(CCMenuItemSpriteExtra* button, int mode, const char* label) {
        if(button == nullptr) return;

        button->setColor(mode == 0 ? ccColor3B{125, 125, 125} : ccColor3B{255, 255, 255});

        if(auto image = button->getNormalImage()) {
            if(auto modeLabel = static_cast<CCLabelBMFont*>(image->getChildByID("mode-label"_spr))) modeLabel->setString(label);
        }
    }

    void bipUpdateButtons() {
        bipUpdateButton(m_fields->m_bipVisibilityBtn, m_fields->m_bipVisibilityMode, s_bipVisLabels[m_fields->m_bipVisibilityMode]);
        bipUpdateButton(m_fields->m_bipSortBtn, m_fields->m_bipSortMode, s_bipSortLabels[m_fields->m_bipSortMode]);
        bipUpdateButton(m_fields->m_bipCompletedBtn, m_fields->m_bipCompletedMode, s_bipCompLabels[m_fields->m_bipCompletedMode]);
    }

    void showBipFilteredText() {
        if(bipIsLevelBrowser() && bipAnyProcessingActive() && this->m_countText) this->m_countText->setString((std::string("(BI+) ") + this->m_countText->getString()).c_str());
    }

    bool shouldSearchForLastPage() {
        return this->m_itemCount == 9999 || BetterInfo::isFalseTotal(this->m_searchObject);
    }

    /**
     * Hooks
     */
    void onGoToPage(CCObject* sender) {
        static_assert(&LevelBrowserLayer::onGoToPage, "Hook not implemented");

        auto popup = SetIDPopup::create(m_searchObject->m_page + 1, 1, 999999, "Go to Page", "Go", true, 1, 60.f, false, false);
        popup->m_delegate = this;
        popup->show();
    }

    void setupPageInfo(gd::string a1, const char* a2) {
        LevelBrowserLayer::setupPageInfo(a1, a2);

        if(this->m_itemCount == 9999 || BetterInfo::isFalseTotal(this->m_searchObject)) this->m_rightArrow->setVisible(true);

        refreshButtonVisibility();
        showFilteredText();
        showBipFilteredText();
    }
    
    void loadLevelsFinished(cocos2d::CCArray* levels, char const* key, int type) {
        auto processed = levels;
        if(bipIsLevelBrowser()) {
            processed = bipProcessLevels(levels);
            m_fields->m_bipCurrentLevels = processed;
        }

        LevelBrowserLayer::loadLevelsFinished(processed, key, type);

        if(m_searchObject->m_searchMode == 1 && m_searchObject->m_searchType == SearchType::Featured && m_searchObject->m_page == 0) {
            if(BetterInfoCache::sharedState()->claimableListsCount() > 0) {
                auto popup = FoundListsPopup::create();
                popup->m_scene = this;
                popup->show();
            }
        }
    }

    void loadPage(GJSearchObject* searchObj) {
        LevelBrowserLayer::loadPage(searchObj);

        refreshButtonVisibility();
    }

    bool init(GJSearchObject* searchObj) {
        if(!LevelBrowserLayer::init(searchObj)) return false;

        if(auto pageMenu = getChildByID("page-menu")) {
            /**
             * Random button
            */
            auto randomSprite = BetterInfo::createWithBISpriteFrameName("BI_randomBtn_001.png");
            randomSprite->setScale(0.9f);
            auto randomBtn = CCMenuItemSpriteExtra::create(
                randomSprite,
                this,
                menu_selector(BILevelBrowserLayer::onLevelBrowserRandom)
            );
            randomBtn->setID("random-button"_spr);
            pageMenu->addChild(randomBtn);

            /**
             * Folder button
            */
            if(!searchObj->isLevelSearchObject()) {
                if(searchObj->m_searchType == SearchType::MyLists || searchObj->m_searchType == SearchType::FavouriteLists) {
                    /*m_folderBtn = CCMenuItemSpriteExtra::create(
                        CCSprite::createWithSpriteFrameName("gj_folderBtn_001.png"), 
                        nullptr, 
                        this,
                        menu_selector(LevelBrowserLayer::onGoToFolder)
                    );

                    m_folderText = CCLabelBMFont::create("0", "bigFont.fnt");
                    m_folderText->setPosition(m_folderBtn->getNormalImage()->getContentSize() / 2);
                    m_folderText->setScale(.55f);
                    m_folderBtn->getNormalImage()->addChild(m_folderText);

                    pageMenu->insertBefore(m_folderBtn, pageMenu->getChildByID("last-page-button"));*/

                    auto folderBtn = FolderButton::create([this](int id) {
                        m_searchObject->m_folder = id;
                        LevelBrowserLayer::loadPage(m_searchObject);
                    });
                    folderBtn->setDisplayFolder(m_searchObject->m_folder);
                    folderBtn->setIsCreated(m_searchObject->m_searchType == SearchType::MyLists);
                    folderBtn->setPopupLabel("Go to Folder");
                    folderBtn->setID("folder-button"_spr);

                    pageMenu->insertBefore(folderBtn, pageMenu->getChildByID("last-page-button"));
                }
            }

            /**
             * Last page button
            */
            if(!BetterInfo::isLocal(this->m_searchObject)){
                m_fields->m_biLastPageBtn = CCMenuItemSpriteExtra::create(
                    m_fields->m_biLastPageBtnArrow = DoubleArrow::create(true, shouldSearchForLastPage()),
                    this,
                    menu_selector(BILevelBrowserLayer::onLevelBrowserLast)
                );
                m_fields->m_biLastPageBtn->setID("last-button"_spr);
                pageMenu->addChild(m_fields->m_biLastPageBtn);
            }

            /**
             * Page menu done
            */
            pageMenu->updateLayout();
        }

        if(auto searchMenu = getChildByID("search-menu")) {
            /**
             * Filter button
            */
            bool isScreenWithoutFilterBtn = !BetterInfo::isLevelSearchObject(m_searchObject) || m_searchObject->m_searchType == SearchType::MyLevels;

            auto filterSprite = CCSprite::createWithSpriteFrameName("GJ_plusBtn_001.png");
            filterSprite->setScale(0.7f);
            auto filterButton = CCMenuItemSpriteExtra::create(
                filterSprite,
                this,
                menu_selector(BILevelBrowserLayer::onLevelBrowserFilter)
            );
            if(!isScreenWithoutFilterBtn) searchMenu->addChild(filterButton);
            filterButton->setID("filter-button"_spr);

            /**
             * First button
            */
            auto firstBtn = CCMenuItemSpriteExtra::create(
                DoubleArrow::create(false),
                this,
                menu_selector(BILevelBrowserLayer::onLevelBrowserFirst)
            );
            firstBtn->setID("first-button"_spr);
            searchMenu->addChild(firstBtn);

            /**
             * BI+ Page sort button
            */
            if(!isScreenWithoutFilterBtn && Mod::get()->getSettingValue<bool>("bip-page-sort")) {
                auto sortSprite = bipFrameSprite("GJ_downloadsIcon_001.png", "GJ_sRecentIcon_001.png");
                sortSprite->setScale(0.9f);
                bipAttachModeLabel(sortSprite, s_bipSortLabels[0]);

                m_fields->m_bipSortBtn = CCMenuItemSpriteExtra::create(
                    sortSprite,
                    this,
                    menu_selector(BILevelBrowserLayer::onBipSort)
                );
                m_fields->m_bipSortBtn->setID("sort-button"_spr);
                searchMenu->addChild(m_fields->m_bipSortBtn);
            }

            /**
             * BI+ Completed filter button
            */
            if(!isScreenWithoutFilterBtn && Mod::get()->getSettingValue<bool>("bip-completed-filter")) {
                auto completedSprite = bipFrameSprite("GJ_completesIcon_001.png", "GJ_starsIcon_001.png");
                completedSprite->setScale(0.8f);
                bipAttachModeLabel(completedSprite, s_bipCompLabels[0]);

                m_fields->m_bipCompletedBtn = CCMenuItemSpriteExtra::create(
                    completedSprite,
                    this,
                    menu_selector(BILevelBrowserLayer::onBipCompleted)
                );
                m_fields->m_bipCompletedBtn->setID("completed-filter-button"_spr);
                searchMenu->addChild(m_fields->m_bipCompletedBtn);
            }

            /**
             * BI+ Copy level IDs button
            */
            if(!isScreenWithoutFilterBtn && Mod::get()->getSettingValue<bool>("bip-copy-ids")) {
                auto copySprite = bipFrameSprite("GJ_duplicateBtn_001.png", "GJ_chatBtn_001.png");
                copySprite->setScale(0.6f);

                auto copyBtn = CCMenuItemSpriteExtra::create(
                    copySprite,
                    this,
                    menu_selector(BILevelBrowserLayer::onBipCopyIds)
                );
                copyBtn->setID("copy-ids-button"_spr);
                searchMenu->addChild(copyBtn);
            }

            /**
             * Search menu done
            */
            searchMenu->updateLayout();
        }

        if(auto infoMenu = getChildByID("info-menu")) {
            /**
             * Star button
            */
            if(!BetterInfo::isStarUseless(this->m_searchObject)) {
                auto starButton = CCMenuItemSpriteExtra::create(
                    CCSprite::createWithSpriteFrameName("GJ_starsIcon_001.png"),
                    this,
                    menu_selector(BILevelBrowserLayer::onLevelBrowserStar)
                );
                if(!(this->m_searchObject->m_starFilter)) starButton->setColor({125,125,125});
                starButton->setZOrder(-2); //place above info btn
                starButton->setID("star-button"_spr);
                infoMenu->addChild(starButton);
                infoMenu->updateLayout();
            }

            if(m_searchObject->m_searchType == SearchType::UsersLevels) {
                auto myId = GameManager::sharedState()->m_playerUserID.value();

                bool isMyLevelUnfiltered = m_searchObject->m_searchQuery == fmt::format("{}", myId);
                bool isMyLevelFiltered = m_searchObject->m_searchQuery == fmt::format("{}&", myId);
                if(isMyLevelUnfiltered || isMyLevelFiltered) {
                    auto hideBtn = CCMenuItemSpriteExtra::create(
                        CCSprite::createWithSpriteFrameName("hideBtn_001.png"),
                        this,
                        menu_selector(BILevelBrowserLayer::onLevelBrowserHide)
                    );
                    hideBtn->setID("unlisted-button"_spr);
                    hideBtn->setZOrder(-3);
                    if(isMyLevelFiltered) hideBtn->setColor({125,125,125});
                    infoMenu->addChild(hideBtn);
                    infoMenu->updateLayout();
                }
            }

            /**
             * BI+ Visibility filter button
            */
            if(bipIsLevelBrowser() && m_searchObject->m_searchType != SearchType::MyLevels && Mod::get()->getSettingValue<bool>("bip-visibility-filter")) {
                auto visibilitySprite = CCSprite::createWithSpriteFrameName("hideBtn_001.png");
                visibilitySprite->setScale(0.9f);
                bipAttachModeLabel(visibilitySprite, s_bipVisLabels[0]);

                m_fields->m_bipVisibilityBtn = CCMenuItemSpriteExtra::create(
                    visibilitySprite,
                    this,
                    menu_selector(BILevelBrowserLayer::onBipVisibility)
                );
                m_fields->m_bipVisibilityBtn->setID("visibility-filter-button"_spr);
                m_fields->m_bipVisibilityBtn->setZOrder(-4);
                infoMenu->addChild(m_fields->m_bipVisibilityBtn);
                infoMenu->updateLayout();
            }
        }

        if(m_searchObject->m_searchMode == 0 && s_labels.contains(m_searchObject->m_searchType)) {
            auto winSize = CCDirector::sharedDirector()->getWinSize();
            auto label = CCSprite::create(s_labels.at(m_searchObject->m_searchType));
            label->setPosition({(winSize.width / 2), (winSize.height / 2) + 24 + 110});
            label->setID("header-sprite"); //substitutes a vanilla feature, therefore vanilla style ID
            this->addChild(label, 2);
        }

        /**
         * Ending steps
        */
        bipUpdateButtons();
        refreshButtonVisibility();

        return true;
    }

    gd::string getSearchTitle() {
        if(m_searchObject->m_folder > 0 && (m_searchObject->m_searchType == SearchType::MyLists || m_searchObject->m_searchType == SearchType::FavouriteLists)) {
            auto folderName = GameLevelManager::sharedState()->getFolderName(m_searchObject->m_folder, m_searchObject->m_searchType == SearchType::MyLists);

            auto mainTitle = m_searchObject->m_searchType == SearchType::MyLists ? "My Lists" : "Favorite Lists";

            if(folderName.empty()) return fmt::format("{} ({})", mainTitle, m_searchObject->m_folder);

            return fmt::format("{} ({}: {})", mainTitle, m_searchObject->m_folder, folderName);
        }

        if(s_labels.contains(m_searchObject->m_searchType)) return "";

        return LevelBrowserLayer::getSearchTitle();
    }

    void onInfo(CCObject* sender) {
        if(s_infoTexts.contains(m_searchObject->m_searchType)) {
            auto info = s_infoTexts.at(m_searchObject->m_searchType);
            return FLAlertLayer::create(
                nullptr,
                info.first,
                info.second,
                "OK",
                nullptr,
                380.f
            )->show();
        }

        return LevelBrowserLayer::onInfo(sender);
    }
};