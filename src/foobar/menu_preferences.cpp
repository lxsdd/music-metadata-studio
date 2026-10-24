#include "stdafx.h"

#include <SDK/coreDarkMode.h>

#include "batch_table_settings.h"

#include "menu_settings.h"
#include "resource.h"

#include <SDK/preferences_page.h>

#include <array>
#include <initializer_list>
#include <limits>
#include <stdexcept>
#include <string>

namespace djmeta_foobar {
namespace {

constexpr std::array<int, menu_caption_count> kControlIDs = {
    IDC_CAPTION_METADATA, IDC_CAPTION_SINGLES, IDC_CAPTION_ALBEN,
    IDC_CAPTION_LIVESETS, IDC_CAPTION_PREPARE
};

std::wstring utf8_to_wide(const std::string& text) {
    if (text.empty()) return {};
    if (text.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)()))
        throw std::invalid_argument("menu label too long");
    const int length = static_cast<int>(text.size());
    const int wide_chars = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        text.data(), length, nullptr, 0);
    if (wide_chars <= 0) throw std::invalid_argument("invalid saved UTF-8 menu label");
    std::wstring result(static_cast<std::size_t>(wide_chars), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        text.data(), length, result.data(), wide_chars) != wide_chars)
        throw std::runtime_error("unable to load menu label");
    return result;
}

std::string wide_to_utf8(const std::wstring& text) {
    if (text.empty()) return {};
    const int length = static_cast<int>(text.size()); // control limit <= 160
    const int bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
        text.data(), length, nullptr, 0, nullptr, nullptr);
    if (bytes <= 0) throw std::invalid_argument("invalid Unicode menu label");
    std::string result(static_cast<std::size_t>(bytes), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
        text.data(), length, result.data(), bytes, nullptr, nullptr) != bytes)
        throw std::runtime_error("unable to save menu label");
    return result;
}

std::wstring read_control(HWND window, int id) {
    HWND control = GetDlgItem(window, id);
    if (!control) throw std::runtime_error("missing menu caption control");
    const int length = GetWindowTextLengthW(control);
    if (length < 0 || length > 160)
        throw std::invalid_argument("menu label length exceeds 160 characters");
    std::wstring value(static_cast<std::size_t>(length) + 1, L'\0');
    const int n = GetWindowTextW(control, value.data(), length + 1);
    value.resize(static_cast<std::size_t>(n));
    return value;
}

bool acceptable_caption(const std::wstring& caption) {
    if (caption.empty() || caption.size() > 160) return false;
    for (const wchar_t ch : caption)
        if (ch < 0x20 || ch == 0x7f) return false;
    return true;
}

constexpr GUID kPreferencesGuid =
    {0x728f3851, 0xbe84, 0x432d, {0xab, 0x07, 0x34, 0xa2, 0xd9, 0x18, 0xe5, 0x0a}};

class MenuPreferencesInstance : public preferences_page_instance {
public:
    MenuPreferencesInstance(fb2k::hwnd_t parent, preferences_page_callback::ptr callback)
        : m_callback(std::move(callback)) {
        m_window = CreateDialogParamW(core_api::get_my_instance(),
            MAKEINTRESOURCEW(IDD_MENU_PREFERENCES), parent,
            dialog_proc, reinterpret_cast<LPARAM>(this));
        if (!m_window)
            throw std::runtime_error("Failed to create Music Metadata Studio Preferences");
    }

    ~MenuPreferencesInstance() {
        if (m_window && IsWindow(m_window)) DestroyWindow(m_window);
    }

    fb2k::hwnd_t get_wnd() override { return m_window; }

    t_uint32 get_state() override {
        t_uint32 flags = preferences_state::resettable |
                         preferences_state::dark_mode_supported;
        try {
            for (unsigned index = 0; index < menu_caption_count; ++index) {
                const auto typed = wide_to_utf8(read_control(m_window, kControlIDs[index]));
                if (typed != effective_menu_caption(index)) {
                    flags |= preferences_state::changed;
                    break;
                }
            }
        } catch (const std::exception&) {
            flags |= preferences_state::changed;
        }
        return flags;
    }

    void apply() override {
        std::array<std::string, menu_caption_count> all;
        try {
            for (unsigned index = 0; index < menu_caption_count; ++index) {
                const std::wstring value = read_control(m_window, kControlIDs[index]);
                if (!acceptable_caption(value))
                    throw std::invalid_argument("Empty or invalid menu caption.");
                all[index] = wide_to_utf8(value);
            }
        } catch (const std::exception&) {
            MessageBoxW(m_window,
                L"All five menu captions must contain 1 to 160 valid characters. "
                L"No settings were changed.",
                L"Music Metadata Studio", MB_OK | MB_ICONWARNING);
            return;
        }

        // Validate the WHOLE form before changing any foobar cfg value.
        for (unsigned index = 0; index < menu_caption_count; ++index)
            set_menu_caption(index, all[index].c_str());
        state_changed();
    }

    void reset() override {
        for (unsigned index = 0; index < menu_caption_count; ++index) {
            const auto text = utf8_to_wide(default_menu_caption(index));
            SetDlgItemTextW(m_window, kControlIDs[index], text.c_str());
        }
        state_changed(); // Reset only stages defaults; Apply persists.
    }

private:
    static INT_PTR CALLBACK dialog_proc(HWND window, UINT msg, WPARAM wp, LPARAM lp) {
        if (msg == WM_INITDIALOG) {
            auto* self = reinterpret_cast<MenuPreferencesInstance*>(lp);
            self->m_window = window; // WM_INITDIALOG precedes CreateDialogParamW returning.
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            self->m_dark.AddDialogWithControls(window);
            try {
                for (unsigned i = 0; i < menu_caption_count; ++i) {
                    SendDlgItemMessageW(window, kControlIDs[i], EM_LIMITTEXT, 160, 0);
                    const auto caption = utf8_to_wide(effective_menu_caption(i));
                    SetDlgItemTextW(window, kControlIDs[i], caption.c_str());
                }
            } catch (const std::exception&) {
                // Invalid or unavailable saved settings are not overwritten
                // until the user explicitly presses Apply.
                for (unsigned i = 0; i < menu_caption_count; ++i) {
                    const auto caption = utf8_to_wide(default_menu_caption(i));
                    SetDlgItemTextW(window, kControlIDs[i], caption.c_str());
                }
            }
            return TRUE;
        }
        auto* self = reinterpret_cast<MenuPreferencesInstance*>(
            GetWindowLongPtrW(window, GWLP_USERDATA));
        if (!self) return FALSE;
        if (msg == WM_COMMAND && HIWORD(wp) == EN_CHANGE) {
            self->state_changed();
            return TRUE;
        }
        if (msg == WM_NCDESTROY) {
            self->m_window = nullptr;
            SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        }
        return FALSE;
    }

    void state_changed() {
        if (m_callback.is_valid()) m_callback->on_state_changed();
    }

    preferences_page_callback::ptr m_callback;
    fb2k::hwnd_t m_window = nullptr;
    fb2k::CCoreDarkModeHooks m_dark;
};

class MenuPreferencesPage : public preferences_page_v3 {
public:
    const char* get_name() override { return "Music Metadata Studio"; }
    GUID get_guid() override { return kPreferencesGuid; }
    GUID get_parent_guid() override { return preferences_page::guid_tools; }
    preferences_page_instance::ptr instantiate(
        fb2k::hwnd_t parent,
        preferences_page_callback::ptr callback) override {
        return fb2k::service_new<MenuPreferencesInstance>(parent, std::move(callback));
    }
};

preferences_page_factory_t<MenuPreferencesPage> g_menu_preferences_factory;

constexpr GUID kWorkspacePreferencesGuid =
    {0xb7e9a4a0, 0x51d0, 0x4268, {0xa4, 0xf7, 0xec, 0x8f, 0x7c, 0x3d, 0xe6, 0xe1}};

djmeta::WorkspaceDefaults read_workspace_controls(HWND window) {
    djmeta::WorkspaceDefaults settings;
    settings.opening_tab = static_cast<int>(SendDlgItemMessageW(
        window, IDC_DEFAULT_START_TAB, CB_GETCURSEL, 0, 0));
    settings.online_provider = static_cast<int>(SendDlgItemMessageW(
        window, IDC_DEFAULT_ONLINE_PROVIDER, CB_GETCURSEL, 0, 0));
    settings.routing_profile = static_cast<int>(SendDlgItemMessageW(
        window, IDC_DEFAULT_ROUTE_PROFILE, CB_GETCURSEL, 0, 0));
    settings.show_whitespace = SendDlgItemMessageW(
        window, IDC_DEFAULT_SHOW_WHITESPACE, BM_GETCHECK, 0, 0) == BST_CHECKED;
    if (!djmeta::workspace_defaults_valid(settings))
        throw std::invalid_argument("Invalid workspace choice.");
    return settings;
}

class WorkspacePreferencesInstance : public preferences_page_instance {
public:
    WorkspacePreferencesInstance(fb2k::hwnd_t parent, preferences_page_callback::ptr callback)
        : m_callback(std::move(callback)) {
        m_window = CreateDialogParamW(core_api::get_my_instance(),
            MAKEINTRESOURCEW(IDD_WORKSPACE_PREFERENCES), parent,
            dialog_proc, reinterpret_cast<LPARAM>(this));
        if (!m_window)
            throw std::runtime_error("Failed to create workspace Preferences.");
    }

    ~WorkspacePreferencesInstance() {
        if (m_window && IsWindow(m_window)) DestroyWindow(m_window);
    }

    fb2k::hwnd_t get_wnd() override { return m_window; }

    t_uint32 get_state() override {
        t_uint32 flags = preferences_state::resettable |
                         preferences_state::dark_mode_supported;
        try {
            if (!(read_workspace_controls(m_window) == load_workspace_defaults()))
                flags |= preferences_state::changed;
        } catch (const std::exception&) {
            flags |= preferences_state::changed;
        }
        return flags;
    }

    void apply() override {
        try {
            const auto edited = read_workspace_controls(m_window);
            store_workspace_defaults(edited);
            state_changed();
        } catch (const std::exception&) {
            MessageBoxW(m_window,
                L"All workspace choices must be valid. No settings were changed.",
                L"Music Metadata Studio", MB_OK | MB_ICONWARNING);
        }
    }

    void reset() override {
        show_settings(m_window, {});
        state_changed(); // Stage only. foobar's Apply/OK performs the save.
    }

private:
    static void add_items(HWND window, int control,
                          std::initializer_list<const wchar_t*> names) {
        for (const wchar_t* name : names) {
            SendDlgItemMessageW(window, control, CB_ADDSTRING, 0,
                reinterpret_cast<LPARAM>(name));
        }
    }

    static void show_settings(HWND window, const djmeta::WorkspaceDefaults& settings) {
        SendDlgItemMessageW(window, IDC_DEFAULT_START_TAB,
            CB_SETCURSEL, settings.opening_tab, 0);
        SendDlgItemMessageW(window, IDC_DEFAULT_ONLINE_PROVIDER,
            CB_SETCURSEL, settings.online_provider, 0);
        SendDlgItemMessageW(window, IDC_DEFAULT_ROUTE_PROFILE,
            CB_SETCURSEL, settings.routing_profile, 0);
        SendDlgItemMessageW(window, IDC_DEFAULT_SHOW_WHITESPACE,
            BM_SETCHECK, settings.show_whitespace ? BST_CHECKED : BST_UNCHECKED, 0);
    }

    static INT_PTR CALLBACK dialog_proc(HWND window, UINT msg, WPARAM wp, LPARAM lp) {
        if (msg == WM_INITDIALOG) {
            auto* self = reinterpret_cast<WorkspacePreferencesInstance*>(lp);
            self->m_window = window;
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            self->m_dark.AddDialogWithControls(window);
            add_items(window, IDC_DEFAULT_START_TAB,
                {L"Find and compare", L"Review proposals", L"Plan file locations"});
            add_items(window, IDC_DEFAULT_ONLINE_PROVIDER,
                {L"MusicBrainz", L"Discogs", L"Beatport + Crates"});
            add_items(window, IDC_DEFAULT_ROUTE_PROFILE,
                {L"Singles", L"Alben", L"Livesets"});
            show_settings(window, load_workspace_defaults());
            return TRUE;
        }
        auto* self = reinterpret_cast<WorkspacePreferencesInstance*>(
            GetWindowLongPtrW(window, GWLP_USERDATA));
        if (!self) return FALSE;
        if (msg == WM_COMMAND) {
            const unsigned notify = HIWORD(wp);
            if (notify == CBN_SELCHANGE || notify == BN_CLICKED) {
                self->state_changed();
                return TRUE;
            }
        }
        if (msg == WM_NCDESTROY) {
            self->m_window = nullptr;
            SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        }
        return FALSE;
    }

    void state_changed() {
        if (m_callback.is_valid()) m_callback->on_state_changed();
    }

    preferences_page_callback::ptr m_callback;
    fb2k::hwnd_t m_window = nullptr;
    fb2k::CCoreDarkModeHooks m_dark;
};

class WorkspacePreferencesPage : public preferences_page_v3 {
public:
    const char* get_name() override { return "Workspace"; }
    GUID get_guid() override { return kWorkspacePreferencesGuid; }
    GUID get_parent_guid() override { return kPreferencesGuid; }
    preferences_page_instance::ptr instantiate(
        fb2k::hwnd_t parent,
        preferences_page_callback::ptr callback) override {
        return fb2k::service_new<WorkspacePreferencesInstance>(
            parent, std::move(callback));
    }
};

preferences_page_factory_t<WorkspacePreferencesPage> g_workspace_preferences_factory;

// Provider preference is independent of the manually selected online mode.
// This list changes display order only for equal-evidence matches. It cannot
// initiate network requests, enable a provider or authorize a metadata write.
constexpr GUID kOnlineProviderPreferencesGuid =
    {0x6b96e414, 0x87d0, 0x412e, {0xa3, 0xf2, 0x68, 0x3f, 0x1b, 0x3e, 0xd8, 0x05}};

constexpr std::array<const wchar_t*, 5> kOnlineProviderLabels = {
    L"Beatport", L"Crates", L"Discogs",
    L"Deezer (lookup not yet available)", L"MusicBrainz"
};

djmeta::online::ProviderPreferences read_provider_order(HWND window) {
    using namespace djmeta::online;
    ProviderPreferences edited;
    edited.preferred_order.clear();
    const auto count = SendDlgItemMessageW(
        window, IDC_PROVIDER_PRIORITY_LIST, LB_GETCOUNT, 0, 0);
    if (count != static_cast<LRESULT>(kProviderNames.size()))
        throw std::invalid_argument("Provider preference list is incomplete.");
    for (int row = 0; row < count; ++row) {
        const auto source = SendDlgItemMessageW(
            window, IDC_PROVIDER_PRIORITY_LIST, LB_GETITEMDATA, row, 0);
        if (source < 0 ||
            source >= static_cast<LRESULT>(kProviderNames.size()))
            throw std::invalid_argument("Provider preference identity is invalid.");
        edited.preferred_order.emplace_back(
            kProviderNames[static_cast<std::size_t>(source)]);
    }
    if (!valid_provider_preferences(edited))
        throw std::invalid_argument("Provider preference is not a permutation.");
    return edited;
}

void show_provider_order(HWND window,
                         const djmeta::online::ProviderPreferences& settings,
                         int selection = 0) {
    using namespace djmeta::online;
    if (!valid_provider_preferences(settings))
        throw std::invalid_argument("Invalid provider preference.");
    SendDlgItemMessageW(window, IDC_PROVIDER_PRIORITY_LIST, LB_RESETCONTENT, 0, 0);
    for (const auto& id : settings.preferred_order) {
        const auto it = std::find(kProviderNames.begin(), kProviderNames.end(), id);
        if (it == kProviderNames.end())
            throw std::invalid_argument("Unknown provider identity.");
        const auto source = static_cast<std::size_t>(
            it - kProviderNames.begin());
        const auto row = SendDlgItemMessageW(
            window, IDC_PROVIDER_PRIORITY_LIST, LB_ADDSTRING, 0,
            reinterpret_cast<LPARAM>(kOnlineProviderLabels[source]));
        if (row == LB_ERR || row == LB_ERRSPACE ||
            SendDlgItemMessageW(window, IDC_PROVIDER_PRIORITY_LIST,
                                LB_SETITEMDATA, row,
                                static_cast<LPARAM>(source)) == LB_ERR)
            throw std::runtime_error("Could not populate provider list.");
    }
    SendDlgItemMessageW(window, IDC_PROVIDER_PRIORITY_LIST,
        LB_SETCURSEL, selection, 0);
}

class ProviderPreferencesInstance : public preferences_page_instance {
public:
    ProviderPreferencesInstance(fb2k::hwnd_t parent,
                                preferences_page_callback::ptr callback)
        : m_callback(std::move(callback)) {
        m_window = CreateDialogParamW(core_api::get_my_instance(),
            MAKEINTRESOURCEW(IDD_PROVIDER_PREFERENCES), parent,
            dialog_proc, reinterpret_cast<LPARAM>(this));
        if (!m_window)
            throw std::runtime_error("Failed to create Online providers Preferences.");
    }

    ~ProviderPreferencesInstance() {
        if (m_window && IsWindow(m_window)) DestroyWindow(m_window);
    }

    fb2k::hwnd_t get_wnd() override { return m_window; }

    t_uint32 get_state() override {
        t_uint32 flags = preferences_state::resettable |
                         preferences_state::dark_mode_supported;
        try {
            if (!(read_provider_order(m_window) == load_online_provider_preferences()))
                flags |= preferences_state::changed;
        } catch (const std::exception&) {
            flags |= preferences_state::changed;
        }
        return flags;
    }

    void apply() override {
        try {
            const auto edited = read_provider_order(m_window);
            store_online_provider_preferences(edited);
            state_changed();
        } catch (const std::exception&) {
            MessageBoxW(m_window,
                L"Source order must include Beatport, Crates, Discogs, "
                L"Deezer and MusicBrainz exactly once. Nothing was changed.",
                L"Music Metadata Studio", MB_OK | MB_ICONWARNING);
        }
    }

    void reset() override {
        show_provider_order(m_window, {});
        state_changed(); // Stage only. Apply/OK persists.
    }

private:
    void move_selection(int direction) {
        const auto row = SendDlgItemMessageW(
            m_window, IDC_PROVIDER_PRIORITY_LIST, LB_GETCURSEL, 0, 0);
        if (row == LB_ERR || row + direction < 0 ||
            row + direction >= static_cast<int>(
                djmeta::online::kProviderNames.size()))
            return;
        auto edited = read_provider_order(m_window);
        std::swap(edited.preferred_order[static_cast<std::size_t>(row)],
                  edited.preferred_order[static_cast<std::size_t>(row + direction)]);
        show_provider_order(m_window, edited, static_cast<int>(row + direction));
        state_changed();
    }

    static INT_PTR CALLBACK dialog_proc(HWND window, UINT msg,
                                        WPARAM wp, LPARAM lp) {
        if (msg == WM_INITDIALOG) {
            auto* self = reinterpret_cast<ProviderPreferencesInstance*>(lp);
            self->m_window = window;
            SetWindowLongPtrW(window, GWLP_USERDATA,
                reinterpret_cast<LONG_PTR>(self));
            self->m_dark.AddDialogWithControls(window);
            show_provider_order(window, load_online_provider_preferences());
            return TRUE;
        }
        auto* self = reinterpret_cast<ProviderPreferencesInstance*>(
            GetWindowLongPtrW(window, GWLP_USERDATA));
        if (!self) return FALSE;
        if (msg == WM_COMMAND && HIWORD(wp) == BN_CLICKED) {
            try {
                if (LOWORD(wp) == IDC_PROVIDER_PRIORITY_UP)
                    self->move_selection(-1);
                else if (LOWORD(wp) == IDC_PROVIDER_PRIORITY_DOWN)
                    self->move_selection(+1);
                else
                    return FALSE;
            } catch (const std::exception&) {
                MessageBoxW(window, L"Unable to reorder source preferences.",
                    L"Music Metadata Studio", MB_OK | MB_ICONWARNING);
            }
            return TRUE;
        }
        if (msg == WM_NCDESTROY) {
            self->m_window = nullptr;
            SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        }
        return FALSE;
    }

    void state_changed() {
        if (m_callback.is_valid()) m_callback->on_state_changed();
    }

    preferences_page_callback::ptr m_callback;
    fb2k::hwnd_t m_window = nullptr;
    fb2k::CCoreDarkModeHooks m_dark;
};

class OnlineProviderPreferencesPage : public preferences_page_v3 {
public:
    const char* get_name() override { return "Online providers"; }
    GUID get_guid() override { return kOnlineProviderPreferencesGuid; }
    GUID get_parent_guid() override { return kPreferencesGuid; }
    preferences_page_instance::ptr instantiate(
        fb2k::hwnd_t parent,
        preferences_page_callback::ptr callback) override {
        return fb2k::service_new<ProviderPreferencesInstance>(
            parent, std::move(callback));
    }
};

preferences_page_factory_t<OnlineProviderPreferencesPage>
    g_online_provider_preferences_factory;

} // namespace
} // namespace djmeta_foobar
