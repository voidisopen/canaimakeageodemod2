#include <Geode/Geode.hpp>
#include <Geode/modify/EditorUI.hpp>
#include <Geode/utils/web.hpp>
#include <Geode/utils/async.hpp>
#include <Geode/ui/TextInput.hpp>
#include <Geode/ui/Notification.hpp>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <string>
#include <vector>

using namespace geode::prelude;

namespace {
constexpr char kApiKeySaveKey[] = "gemini-api-key";
constexpr char kGeminiEndpoint[] =
    "https://generativelanguage.googleapis.com/v1beta/models/gemini-3.6-flash:generateContent";
constexpr size_t kMaxDecorations = 64;

struct Decoration {
    int objectID;
    float x;
    float y;
    float scale;
    float rotation;
};

void showError(std::string const& message) {
    FLAlertLayer::create("Gemini Deco", message, "OK")->show();
}

void applyLayout(Ref<LevelEditorLayer> editor, std::vector<Decoration> decorations) {
    if (!editor || LevelEditorLayer::get() != editor) {
        showError("The level editor was closed before the layout could be applied.");
        return;
    }

    // center of the visible screen, expressed in object-layer (level) coordinates
    auto viewSize = CCDirector::get()->getWinSize();
    auto cameraCenter = editor->m_objectLayer->convertToNodeSpace(
        ccp(viewSize.width / 2.f, viewSize.height / 2.f)
    );
    size_t added = 0;
    for (auto const& decoration : decorations) {
        auto check = GameObject::createWithKey(decoration.objectID);
        if (!check || check->m_objectType != GameObjectType::Decoration) continue;

        auto object = editor->createObject(
            decoration.objectID,
            cameraCenter + ccp(decoration.x, decoration.y),
            false
        );
        if (!object || object->m_objectType != GameObjectType::Decoration) continue;

        object->setScale(decoration.scale);
        object->setRotation(decoration.rotation);
        ++added;
    }

    if (added == 0) {
        showError("Gemini did not return any valid decoration objects. Try a more specific prompt.");
        return;
    }

    FLAlertLayer::create(
        "Gemini Deco",
        fmt::format("Added {} decoration objects. Use Undo in the editor to remove them.", added),
        "OK"
    )->show();
}

class GeminiDecoPopup final : public geode::Popup {
protected:
    TextInput* m_keyInput = nullptr;
    TextInput* m_promptInput = nullptr;

    bool init() {
        if (!Popup::init(380.f, 235.f)) return false;
        this->setTitle("Gemini Deco");

        auto keyLabel = CCLabelBMFont::create("Gemini API key", "goldFont.fnt");
        keyLabel->setScale(.55f);
        keyLabel->setAnchorPoint({ 0.f, .5f });
        keyLabel->setPosition(28.f, 174.f);
        m_mainLayer->addChild(keyLabel);

        m_keyInput = TextInput::create(320.f, "Paste your Gemini API key", "bigFont.fnt");
        m_keyInput->setPasswordMode(true);
        m_keyInput->setMaxCharCount(256);
        m_keyInput->setString(Mod::get()->getSavedValue<std::string>(kApiKeySaveKey, ""));
        m_keyInput->setPosition(190.f, 143.f);
        m_mainLayer->addChild(m_keyInput);

        auto promptLabel = CCLabelBMFont::create("Describe the decoration", "goldFont.fnt");
        promptLabel->setScale(.55f);
        promptLabel->setAnchorPoint({ 0.f, .5f });
        promptLabel->setPosition(28.f, 105.f);
        m_mainLayer->addChild(promptLabel);

        m_promptInput = TextInput::create(320.f, "e.g. ornate neon forest ruins", "bigFont.fnt");
        m_promptInput->setMaxCharCount(180);
        m_promptInput->setPosition(190.f, 75.f);
        m_mainLayer->addChild(m_promptInput);

        auto privacyLabel = CCLabelBMFont::create("Key stays on this PC and is sent to Google.", "bigFont.fnt");
        privacyLabel->setScale(.38f);
        privacyLabel->setOpacity(190);
        privacyLabel->setPosition(190.f, 43.f);
        m_mainLayer->addChild(privacyLabel);

        auto menu = CCMenu::create();
        auto generateButton = CCMenuItemExt::createSpriteExtra(
            ButtonSprite::create("Generate"),
            [this](CCMenuItemSpriteExtra*) { this->onGenerate(); }
        );
        auto forgetButton = CCMenuItemExt::createSpriteExtra(
            ButtonSprite::create("Forget key"),
            [this](CCMenuItemSpriteExtra*) {
                Mod::get()->setSavedValue<std::string>(kApiKeySaveKey, "");
                m_keyInput->setString("");
            }
        );
        menu->addChild(generateButton);
        menu->addChild(forgetButton);
        menu->alignItemsHorizontallyWithPadding(12.f);
        menu->setPosition(190.f, 16.f);
        m_mainLayer->addChild(menu);
        return true;
    }

    void onGenerate() {
        auto apiKey = std::string(m_keyInput->getString());
        {
            auto notSpace = [](unsigned char c) { return !std::isspace(c); };
            apiKey.erase(apiKey.begin(), std::find_if(apiKey.begin(), apiKey.end(), notSpace));
            apiKey.erase(std::find_if(apiKey.rbegin(), apiKey.rend(), notSpace).base(), apiKey.end());
        }
        auto prompt = std::string(m_promptInput->getString());
        if (apiKey.empty()) {
            showError("Enter a Gemini API key first.");
            return;
        }
        if (prompt.empty()) {
            showError("Describe the style or theme you want.");
            return;
        }

        Mod::get()->setSavedValue<std::string>(kApiKeySaveKey, apiKey);
        auto editor = Ref<LevelEditorLayer>(LevelEditorLayer::get());
        if (!editor) {
            showError("Open a level in the editor first.");
            return;
        }

        auto bodyResult = matjson::parse(
            R"({"contents":[{"parts":[{"text":""}]}],"generationConfig":{"responseMimeType":"application/json","maxOutputTokens":8192}})"
        );
        if (bodyResult.isErr()) {
            showError("Could not prepare the Gemini request.");
            return;
        }

        auto body = bodyResult.unwrap();
        body["contents"][0]["parts"][0]["text"] = fmt::format(
            "Create a polished Geometry Dash level decoration layout for this request: {}. "
            "Return JSON only, with this shape: {{\"description\": string, \"objects\": "
            "[{{\"object_id\": integer, \"x\": number, \"y\": number, "
            "\"scale\": number, \"rotation\": number}}]}}. "
            "Use only real Geometry Dash objects whose type is Decoration, never blocks, hazards, "
            "triggers, portals, or gameplay objects. Make a cohesive composition with deliberate "
            "spacing, varied scale and rotation, and a strong focal point. Return at most 64 objects. "
            "x and y are offsets in editor units from the current view center; keep x between -700 and 700 "
            "and y between -260 and 260. Keep scale between 0.25 and 2 and rotation between -360 and 360.",
            prompt
        );

        auto request = web::WebRequest();
        request.header("x-goog-api-key", apiKey);
        request.header("Content-Type", "application/json");
        request.timeout(std::chrono::seconds(45));
        request.bodyJSON(body);

        async::spawn(
            request.post(kGeminiEndpoint),
            [editor = std::move(editor), keyLen = apiKey.size()](web::WebResponse response) mutable {
                if (!response.ok()) {
                    std::string detail;
                    auto errBody = response.string();
                    if (errBody.isOk()) {
                        auto parsedErr = matjson::parse(errBody.unwrap());
                        if (parsedErr.isOk()) {
                            auto errRoot = parsedErr.unwrap();
                            auto msg = errRoot["error"]["message"].asString();
                            if (msg.isOk()) detail = msg.unwrap();
                        }
                    }
                    showError(fmt::format("Gemini request failed (HTTP {}).\n{}\n(Key length in game: {}; a normal key is about 39)", response.code(), detail, keyLen));
                    return;
                }

                auto responseJSON = response.json();
                if (responseJSON.isErr()) {
                    showError("Gemini returned a response that could not be read.");
                    return;
                }

                auto root = responseJSON.unwrap();
                auto candidatesResult = root["candidates"].asArray();
                if (candidatesResult.isErr() || candidatesResult.unwrap().empty()) {
                    showError("Gemini did not return a layout. Try changing your prompt.");
                    return;
                }

                auto candidates = candidatesResult.unwrap();
                auto partsResult = candidates.front()["content"]["parts"].asArray();
                if (partsResult.isErr() || partsResult.unwrap().empty()) {
                    showError("Gemini did not return a layout. Try changing your prompt.");
                    return;
                }
                auto textResult = partsResult.unwrap().front()["text"].asString();
                if (textResult.isErr()) {
                    showError("Gemini did not return a layout description.");
                    return;
                }

                auto layoutResult = matjson::parse(textResult.unwrap());
                if (layoutResult.isErr()) {
                    showError("Gemini's layout was not valid JSON. Try generating again.");
                    return;
                }

                auto layout = layoutResult.unwrap();
                auto objectsResult = layout["objects"].asArray();
                if (objectsResult.isErr()) {
                    showError("Gemini's layout did not contain a decoration list.");
                    return;
                }

                std::vector<Decoration> decorations;
                for (auto const& item : objectsResult.unwrap()) {
                    if (decorations.size() >= kMaxDecorations) break;

                    auto idResult = item.get<int>("object_id");
                    auto xResult = item.get<double>("x");
                    auto yResult = item.get<double>("y");
                    auto scaleResult = item.get<double>("scale");
                    auto rotationResult = item.get<double>("rotation");
                    if (idResult.isErr() || xResult.isErr() || yResult.isErr() ||
                        scaleResult.isErr() || rotationResult.isErr()) {
                        continue;
                    }
                    double x = xResult.unwrap(), y = yResult.unwrap();
                    double scale = scaleResult.unwrap(), rotation = rotationResult.unwrap();
                    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(scale) || !std::isfinite(rotation)) {
                        continue;
                    }

                    int objectID = idResult.unwrap();
                    if (objectID < 1 || objectID > 6000) continue;

                    decorations.push_back({
                        objectID,
                        static_cast<float>(std::clamp(x, -700.0, 700.0)),
                        static_cast<float>(std::clamp(y, -260.0, 260.0)),
                        static_cast<float>(std::clamp(scale, 0.25, 2.0)),
                        static_cast<float>(std::clamp(rotation, -360.0, 360.0))
                    });
                }

                if (decorations.empty()) {
                    showError("Gemini returned no usable decorations. Try a more specific prompt.");
                    return;
                }

                auto description = layout["description"].asString().unwrapOr("Gemini decoration layout");
                createQuickPopup(
                    "Apply Decoration",
                    fmt::format("{}\n\n{} decoration objects will be added to the current view.", description, decorations.size()),
                    "Cancel", "Apply",
                    [editor = std::move(editor), decorations = std::move(decorations)](FLAlertLayer*, bool apply) mutable {
                        if (apply) applyLayout(std::move(editor), std::move(decorations));
                    }
                );
            }
        );

        Notification::create("Generating your decoration...", NotificationIcon::Loading, 3.f)->show();
    }

public:
    static GeminiDecoPopup* create() {
        auto popup = new GeminiDecoPopup();
        if (popup->init()) {
            popup->autorelease();
            return popup;
        }
        delete popup;
        return nullptr;
    }
};
}

class $modify(GeminiDecoEditorUI, EditorUI) {
    bool init(LevelEditorLayer* editorLayer) {
        if (!EditorUI::init(editorLayer)) return false;

        auto menu = CCMenu::create();
        auto button = CCMenuItemExt::createSpriteExtra(
            ButtonSprite::create("AI Deco"),
            [](CCMenuItemSpriteExtra*) { GeminiDecoPopup::create()->show(); }
        );
        button->setScale(.65f);
        menu->addChild(button);

        auto size = CCDirector::get()->getWinSize();
        menu->setPosition(58.f, size.height - 42.f);
        menu->setID("gemini-deco-button-menu");
        this->addChild(menu, 100);
        return true;
    }
};
