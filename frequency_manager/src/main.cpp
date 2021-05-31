#include <imgui.h>
#include <spdlog/spdlog.h>
#include <module.h>
#include <gui/gui.h>
#include <gui/style.h>
#include <core.h>
#include <thread>
#include <radio_interface.h>
#include <signal_path/signal_path.h>
#include <vector>

SDRPP_MOD_INFO {
    /* Name:            */ "frequency_manager",
    /* Description:     */ "Frequency manager module for SDR++",
    /* Author:          */ "Ryzerth;zimm",
    /* Version:         */ 0, 3, 0,
    /* Max instances    */ -1
};

struct FrequencyBookmark {
    double frequency;
    double bandwidth;
    int mode;
    bool selected;
};

class FrequencyManagerModule : public ModuleManager::Instance {
public:
    FrequencyManagerModule(std::string name) {
        this->name = name;

        gui::menu.registerEntry(name, menuHandler, this, NULL);
    }

    ~FrequencyManagerModule() {
        gui::menu.removeEntry(name);
    }

    void enable() {
        enabled = true;
    }

    void disable() {
        enabled = false;
    }

    bool isEnabled() {
        return enabled;
    }

private:
    static std::string freqToStr(double freq) {
        char str[128];
        if (freq >= 1000000.0) {
            sprintf(str, "%.06lf", freq / 1000000.0);
            int len = strlen(str) - 1;
            while ((str[len] == '0' || str[len] == '.') && len > 0) { len--; }
            return std::string(str).substr(0, len + 1) + "MHz";
        }
        else if (freq >= 1000.0) {
            sprintf(str, "%.06lf", freq / 1000.0);
            int len = strlen(str) - 1;
            while ((str[len] == '0' || str[len] == '.') && len > 0) { len--; }
            return std::string(str).substr(0, len + 1) + "KHz";
        }
        else {
            sprintf(str, "%.06lf", freq);
            int len = strlen(str) - 1;
            while ((str[len] == '0' || str[len] == '.') && len > 0) { len--; }
            return std::string(str).substr(0, len + 1) + "Hz";
        }
    }

    static void applyBookmark(FrequencyBookmark bm, std::string vfoName) {
        if (vfoName == "") {
            // TODO: Replace with proper tune call
            gui::waterfall.setCenterFrequency(bm.frequency);
            gui::waterfall.centerFreqMoved = true;
        }
        else {

        }
    }

    bool bookmarkEditDialog(FrequencyBookmark& bm) {
        bool open = true;
        std::string id = "Edit##freq_manager_edit_popup_" + name;
        ImGui::OpenPopup(id.c_str());
        FrequencyBookmark tmp = bm;
        if (ImGui::BeginPopup(id.c_str(), ImGuiWindowFlags_NoResize)) {
            if (ImGui::Button("Apply")) {
                bm = tmp;
                open = false;
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                open = false;
            }
            ImGui::EndPopup();
        }
        return open;
    }

    static void menuHandler(void* ctx) {
        FrequencyManagerModule* _this = (FrequencyManagerModule*)ctx;
        float menuWidth = ImGui::GetContentRegionAvailWidth();

        // TODO: Replace with something that won't iterate every frame
        std::vector<std::string> selectedNames;
        for (auto& [name, bm] : _this->bookmarks) { if (bm.selected) { selectedNames.push_back(name); } }

        //Draw buttons on top of the list
        ImGui::BeginTable(("freq_manager_btn_table"+_this->name).c_str(), 3);
        ImGui::TableNextRow();

        ImGui::TableSetColumnIndex(0);
        if (ImGui::Button(("Add##_freq_mgr_add_" + _this->name).c_str(), ImVec2(ImGui::GetContentRegionAvailWidth(), 0))) {
            // If there's no VFO selected, just save the center freq
            FrequencyBookmark bm;
            if (gui::waterfall.selectedVFO == "") {
                bm.frequency = gui::waterfall.getCenterFrequency();
                bm.bandwidth = 0;
                bm.mode = -1;
            }
            else {
                bm.frequency = gui::waterfall.getCenterFrequency() + sigpath::vfoManager.getOffset(gui::waterfall.selectedVFO);
                bm.bandwidth = sigpath::vfoManager.getBandwidth(gui::waterfall.selectedVFO);
                bm.mode = -1;
                if (core::modComManager.getModuleName(gui::waterfall.selectedVFO) == "radio") {
                    int mode;
                    core::modComManager.callInterface(gui::waterfall.selectedVFO, RADIO_IFACE_CMD_GET_MODE, NULL, &mode);
                    bm.mode = mode;
                }
            }

            bm.selected = false;

            char name[1024];
            sprintf(name, "Test Bookmark (%d)", _this->testN);
            _this->bookmarks[name] = bm;
            _this->testN++;

            _this->editOpen = true;
            _this->editedBookmarkName = name;
        }

        ImGui::TableSetColumnIndex(1);
        if (ImGui::Button(("Remove##_freq_mgr_rem_" + _this->name).c_str(), ImVec2(ImGui::GetContentRegionAvailWidth(), 0))) {
            for (auto& name : selectedNames) { _this->bookmarks.erase(name); }
        }

        ImGui::TableSetColumnIndex(2);
        if (selectedNames.size() != 1) { style::beginDisabled(); }
        if (ImGui::Button(("Edit##_freq_mgr_edt_" + _this->name).c_str(), ImVec2(ImGui::GetContentRegionAvailWidth(), 0))) {
            _this->editOpen = true;
            _this->editedBookmarkName = selectedNames[0];
        }
        if (selectedNames.size() != 1) { style::endDisabled(); }
        
        ImGui::EndTable();

        // Bookmark list
        ImGui::BeginTable(("freq_manager_bkm_table"+_this->name).c_str(), 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg, ImVec2(0, 300));
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Bookmark", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        for (auto& [name, bm] : _this->bookmarks) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImVec2 min = ImGui::GetCursorPos();

            ImGui::Selectable((name + "##_freq_mgr_bkm_name_" + _this->name).c_str(), &bm.selected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_SelectOnClick);
            if (ImGui::TableGetHoveredColumn() >= 0 && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                applyBookmark(bm, gui::waterfall.selectedVFO);
            }

            ImGui::TableSetColumnIndex(1);
            ImGui::Text(freqToStr(bm.frequency).c_str());
            ImVec2 max = ImGui::GetCursorPos();
            
            
        }
        
        ImGui::EndTable();

        if (selectedNames.size() != 1) { style::beginDisabled(); }
        if (ImGui::Button(("Apply##_freq_mgr_apply_" + _this->name).c_str(), ImVec2(menuWidth, 0))) {
            FrequencyBookmark& bm = _this->bookmarks[selectedNames[0]];
            applyBookmark(bm, gui::waterfall.selectedVFO);
            bm.selected = false;
        }
        if (selectedNames.size() != 1) { style::endDisabled(); }

        if (_this->editOpen) {
            FrequencyBookmark& bm = _this->bookmarks[_this->editedBookmarkName];
            _this->editOpen = _this->bookmarkEditDialog(bm);
        }
        if (_this->addOpen) {
            FrequencyBookmark& bm = _this->bookmarks[_this->editedBookmarkName];
            _this->addOpen = _this->bookmarkEditDialog(bm);
        }
    }

    std::string name;
    bool enabled = true;
    bool editOpen = false;
    bool addOpen = false;

    std::map<std::string, FrequencyBookmark> bookmarks;

    std::string editedBookmarkName = "";

    int testN = 0;

};

MOD_EXPORT void _INIT_() {
    // Nothing here (testing)
}

MOD_EXPORT ModuleManager::Instance* _CREATE_INSTANCE_(std::string name) {
    return new FrequencyManagerModule(name);
}

MOD_EXPORT void _DELETE_INSTANCE_(void* instance) {
    delete (FrequencyManagerModule*)instance;
}

MOD_EXPORT void _END_() {
    // Nothing here
}