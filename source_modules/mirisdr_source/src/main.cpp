#include <utils/flog.h>
#include <module.h>
#include <gui/gui.h>
#include <signal_path/signal_path.h>
#include <core.h>
#include <gui/style.h>
#include <config.h>
#include <gui/widgets/stepped_slider.h>
#include <gui/smgui.h>

#include <thread>

#ifdef __ANDROID__
#include <android_backend.h>
#endif

#include <mirisdr.h>

#define CONCAT(a, b) ((std::string(a) + b).c_str())

SDRPP_MOD_INFO{
    /* Name:            */ "mirisdr_source",
    /* Description:     */ "Mirisdr source module for SDR++",
    /* Author:          */ "cropinghigh",
    /* Version:         */ 1, 0, 0,
    /* Max instances    */ 1
};

ConfigManager config;

const char* sampleRatesTxt = "15 MHz\0"
                             "14 MHz\0"
                             "13 MHz\0"
                             "12 MHz\0"
                             "11 MHz\0"
                             "10 MHz\0"
                             "9 MHz\0"
                             "8 MHz\0"
                             "7 MHz\0"
                             "6 MHz\0"
                             "5 MHz\0"
                             "4 MHz\0"
                             "3 MHz\0"
                             "2 MHz\0"
                             "1.54 MHz\0";

const int sampleRates[] = {
    15000000,
    14000000,
    13000000,
    12000000,
    11000000,
    10000000,
    9000000,
    8000000,
    7000000,
    6000000,
    5000000,
    4000000,
    3000000,
    2000000,
    1540000,
};

const int bandwidths[] = {
    14000000,
    8000000,
    7000000,
    6000000,
    5000000,
    1536000,
    600000,
    300000,
    200000,
};

const char* bandwidthsTxt = "14 MHz\0"
                            "8 MHz\0"
                            "7 MHz\0"
                            "6 MHz\0"
                            "5 MHz\0"
                            "1.536 MHz\0"
                            "600 kHz\0"
                            "300 kHz\0"
                            "200 kHz\0";

class MirisdrSourceModule : public ModuleManager::Instance {
public:
    MirisdrSourceModule(std::string name) {
        this->name = name;

        // TODO Select the last samplerate option
        sampleRate = 1540000;
        srId = 14;

        handler.ctx = this;
        handler.selectHandler = menuSelected;
        handler.deselectHandler = menuDeselected;
        handler.menuHandler = menuHandler;
        handler.startHandler = start;
        handler.stopHandler = stop;
        handler.tuneHandler = tune;
        handler.stream = &stream;

        refresh();

        config.acquire();
        std::string confSerial = config.conf["device"];
        config.release();
        selectBySerial(confSerial);

        sigpath::sourceManager.registerSource("Mirisdr", &handler);
    }

    ~MirisdrSourceModule() {
        stop(this);
        sigpath::sourceManager.unregisterSource("Mirisdr");
    }

    void postInit() {}

    void enable() {
        enabled = true;
    }

    void disable() {
        enabled = false;
    }

    bool isEnabled() {
        return enabled;
    }

    void refresh() {
        devList.clear();
        devIdxList.clear();
        devListTxt = "";

        int cnt = 0;

#ifndef __ANDROID__
        cnt = mirisdr_get_device_count();

        for(int i = 0; i < cnt; i++) {
            char manufact[256];
            char product[256];
            char serial[256];
            if(!mirisdr_get_device_usb_strings(i, manufact, product, serial)) {
                std::string name = std::string(manufact) + " " + std::string(product) + " " + std::string(serial);
                devList.push_back(name);
                devIdxList.push_back(i);
                devListTxt += name;
                devListTxt += '\0';
            }
        }
#else
        // Check for device connection
        cnt = 0;
        int vid, pid;
        devFd = backend::getDeviceFD(vid, pid, backend::MIRISDR_VIDPIDS);
        if (devFd < 0) { return; }
        // Generate fake device info
        devList.push_back("fake fake fake");
        devIdxList.push_back(-1);
        devListTxt += "RSP1-compat. USB dongle";
        devListTxt += '\0';
#endif
    }

    void selectFirst() {
        if (devList.size() != 0) {
            selectBySerial(devList[0]);
            return;
        }
        selectedSerial = "";
    }

    void selectBySerial(std::string serial) {
        if (std::find(devList.begin(), devList.end(), serial) == devList.end()) {
            selectFirst();
            return;
        }

        bool created = false;
        config.acquire();
        if (!config.conf["devices"].contains(serial)) {
            config.conf["devices"][serial]["sampleRate"] = 1540000;
            config.conf["devices"][serial]["bandwidth"] = 3;
        }
        config.release(created);

        // Set default values
        srId = 14;
        sampleRate = 1540000;
        bwId = 3;

        // Load from config if available and validate
        if (config.conf["devices"][serial].contains("sampleRate")) {
            int psr = config.conf["devices"][serial]["sampleRate"];
            for (int i = 0; i < 14; i++) {
                if (sampleRates[i] == psr) {
                    sampleRate = psr;
                    srId = i;
                }
            }
        }

        if (config.conf["devices"][serial].contains("gain_lna")) {
            gain_lna = config.conf["devices"][serial]["gain_lna"];
        }
        if (config.conf["devices"][serial].contains("gain_mixer")) {
            gain_mixer = config.conf["devices"][serial]["gain_mixer"];
        }
        if (config.conf["devices"][serial].contains("gain_mb")) {
            gain_mb = config.conf["devices"][serial]["gain_mb"];
        }
        if (config.conf["devices"][serial].contains("gain_bb")) {
            gain_bb = config.conf["devices"][serial]["gain_bb"];
        }
        if (config.conf["devices"][serial].contains("biasTee")) {
            biasTee = config.conf["devices"][serial]["biasTee"];
        }

        if (config.conf["devices"][serial].contains("bandwidth")) {
            bwId = config.conf["devices"][serial]["bandwidth"];
            bwId = std::clamp<int>(bwId, 0, 7);
        }

        selectedSerial = serial;
    }

private:
    static void menuSelected(void* ctx) {
        MirisdrSourceModule* _this = (MirisdrSourceModule*)ctx;
        core::setInputSampleRate(_this->sampleRate);
        flog::info("MirisdrSourceModule '{0}': Menu Select!", _this->name);
    }

    static void menuDeselected(void* ctx) {
        MirisdrSourceModule* _this = (MirisdrSourceModule*)ctx;
        flog::info("MirisdrSourceModule '{0}': Menu Deselect!", _this->name);
    }

    int bandwidthIdToBw(int id) {
        return bandwidths[id];
    }

    static void start(void* ctx) {
        MirisdrSourceModule* _this = (MirisdrSourceModule*)ctx;
        if (_this->running) { return; }
        if (_this->selectedSerial == "") {
            flog::error("Tried to start Mirisdr source with empty serial");
            return;
        }

        _this->selectBySerial(_this->selectedSerial);

        int cnt = mirisdr_get_device_count();
        int id = -1;

        // find the device index from serial
        auto serialIt = std::find(_this->devList.cbegin(), _this->devList.cend(), _this->selectedSerial);
        if (serialIt != _this->devList.cend()) {
            id = _this->devIdxList[std::distance(_this->devList.cbegin(), serialIt)];
        }

#ifndef __ANDROID__
        if(id == -1) {
            flog::error("Mirisdr device is not available");
            return;
        }

        int oret = mirisdr_open(&_this->openDev, id);
#else
        int oret = mirisdr_open_fd(&_this->openDev, _this->devFd);
#endif

        if(oret) {
            flog::error("Could not open Mirisdr {0} id {1} cnt {2}", _this->selectedSerial, id, cnt);
            return;
        }
        if(mirisdr_set_hw_flavour(_this->openDev, MIRISDR_HW_DEFAULT)) {
            flog::error("Could not set Mirisdr hw flavour {0}", _this->selectedSerial);
            return;
        }
        if(mirisdr_set_sample_format(_this->openDev, "AUTO")) {
            flog::error("Could not set Mirisdr sample format {0}", _this->selectedSerial);
            return;
        }
        if(mirisdr_set_transfer(_this->openDev, "BULK")) {
            flog::error("Could not set Mirisdr transfer {0}", _this->selectedSerial);
            return;
        }
        if(mirisdr_set_if_freq(_this->openDev, 0)) {
            flog::error("Could not set Mirisdr if freq {0}", _this->selectedSerial);
            return;
        }
        if(mirisdr_set_sample_rate(_this->openDev, _this->sampleRate)) {
            flog::error("Could not set Mirisdr sample rate {0}", _this->selectedSerial);
            return;
        }
        if(mirisdr_set_bandwidth(_this->openDev, _this->bandwidthIdToBw(_this->bwId))) {
            flog::error("Could not set Mirisdr bandwidth {0}", _this->selectedSerial);
            return;
        }
        if(mirisdr_set_center_freq(_this->openDev, _this->freq)) {
            flog::error("Could not set Mirisdr center freq {0}", _this->selectedSerial);
            return;
        }

        if(mirisdr_set_mixer_gain(_this->openDev, _this->gain_mixer)) {
            flog::error("Could not set Mirisdr gain {0}", _this->selectedSerial);
            return;
        }
        if(mirisdr_set_lna_gain(_this->openDev, _this->gain_lna)) {
            flog::error("Could not set Mirisdr gain {0}", _this->selectedSerial);
            return;
        }
        if(mirisdr_set_mixbuffer_gain(_this->openDev, _this->gain_mb)) {
            flog::error("Could not set Mirisdr gain {0}", _this->selectedSerial);
            return;
        }
        if(mirisdr_set_baseband_gain(_this->openDev, _this->gain_bb)) {
            flog::error("Could not set Mirisdr gain {0}", _this->selectedSerial);
            return;
        }
        if(mirisdr_set_bias(_this->openDev, _this->biasTee ? 1 : 0)) {
            flog::error("Could not set Mirisdr biasTee {0}", _this->selectedSerial);
            return;
        }

        /* Reset endpoint before we start reading from it (mandatory) */
        if(mirisdr_reset_buffer(_this->openDev)) {
            flog::error("Failed to reset Mirisdr buffer {0}", _this->selectedSerial);
            return;
        }

        _this->workerThread = std::thread(mirisdr_read_async, _this->openDev, callback, _this, 0, (_this->sampleRate/50)*sizeof(int16_t));

        _this->running = true;

        _this->updateGains();

        flog::info("MirisdrSourceModule '{0}': Start!", _this->name);
    }

    static void stop(void* ctx) {
        MirisdrSourceModule* _this = (MirisdrSourceModule*)ctx;
        if (!_this->running) { return; }
        _this->running = false;
        if(mirisdr_cancel_async(_this->openDev)) {
            flog::error("Mirisdr async cancel failed {0}", _this->selectedSerial);
        }
        _this->stream.stopWriter();
        _this->workerThread.join();
        int err = mirisdr_close(_this->openDev);
        if (err) {
            flog::error("Could not close Mirisdr {0}", _this->selectedSerial);
        }
        _this->stream.clearWriteStop();
        flog::info("MirisdrSourceModule '{0}': Stop!", _this->name);
    }

    static void tune(double freq, void* ctx) {
        MirisdrSourceModule* _this = (MirisdrSourceModule*)ctx;
        if (_this->running) {
            if(mirisdr_set_center_freq(_this->openDev, (uint32_t)freq) || mirisdr_get_center_freq(_this->openDev) != (uint32_t)freq) {
                flog::error("Could not set Mirisdr freq {0}(selected {1}, current {2})", _this->selectedSerial, (uint32_t)freq, mirisdr_get_center_freq(_this->openDev));
            }
        }
        _this->freq = freq;
        flog::info("MirisdrSourceModule '{0}': Tune: {1}!", _this->name, (uint32_t)freq);
    }

    static void menuHandler(void* ctx) {
        MirisdrSourceModule* _this = (MirisdrSourceModule*)ctx;

        if (_this->running) { SmGui::BeginDisabled(); }
        SmGui::FillWidth();
        SmGui::ForceSync();
        if (SmGui::Combo(CONCAT("##_mirisdr_dev_sel_", _this->name), &_this->devId, _this->devListTxt.c_str())) {
            _this->selectBySerial(_this->devList[_this->devId]);
            config.acquire();
            config.conf["device"] = _this->selectedSerial;
            config.release(true);
        }

        if (SmGui::Combo(CONCAT("##_mirisdr_sr_sel_", _this->name), &_this->srId, sampleRatesTxt)) {
            _this->sampleRate = sampleRates[_this->srId];
            core::setInputSampleRate(_this->sampleRate);
            config.acquire();
            config.conf["devices"][_this->selectedSerial]["sampleRate"] = _this->sampleRate;
            config.release(true);
        }

        SmGui::SameLine();
        SmGui::FillWidth();
        SmGui::ForceSync();
        if (SmGui::Button(CONCAT("Refresh##_mirisdr_refr_", _this->name))) {
            _this->refresh();
            _this->selectBySerial(_this->selectedSerial);
            core::setInputSampleRate(_this->sampleRate);
        }

        if (_this->running) { SmGui::EndDisabled(); }

        SmGui::LeftLabel("Bandwidth");
        SmGui::FillWidth();
        if (SmGui::Combo(CONCAT("##_mirisdr_bw_sel_", _this->name), &_this->bwId, bandwidthsTxt)) {
            if (_this->running) {
                mirisdr_set_bandwidth(_this->openDev, _this->bandwidthIdToBw(_this->bwId));
            }
            config.acquire();
            config.conf["devices"][_this->selectedSerial]["bandwidth"] = _this->bwId;
            config.release(true);
        }

        bool shouldUpdateGains = false;

        if (!_this->running) { SmGui::BeginDisabled(); }
        SmGui::LeftLabel("Gain");
        SmGui::FillWidth();
        int autoGain = 0;
        if (_this->running) {
            autoGain = mirisdr_get_tuner_gain(_this->openDev);
        }

        if (SmGui::SliderInt(CONCAT("##_mirisdr_gain_", _this->name), &autoGain, 0, 102)) {
            if (_this->running) {
                mirisdr_set_tuner_gain(_this->openDev, autoGain);
            }
            shouldUpdateGains = true;
        }
        if (!_this->running) { SmGui::EndDisabled(); }

        float menuWidth, mixerCursorY;
        if (!_this->serverMode) {
            menuWidth = ImGui::GetContentRegionAvail().x;
            mixerCursorY = ImGui::GetCursorPosY();
        }

        SmGui::LeftLabel("Mixer gain");
        if (SmGui::Checkbox(CONCAT("##_mirisdr_mixergain_", _this->name), &_this->gain_mixer)) {
            if (_this->running) {
                if (mirisdr_set_mixer_gain(_this->openDev, _this->gain_mixer)) {
                    flog::error("Could not set Mirisdr gain {0}", _this->selectedSerial);
                }
            }
            shouldUpdateGains = true;
        }

        if (_this->serverMode) {
            SmGui::FillWidth();
        } else {
            auto& style = ImGui::GetStyle();
            ImGui::SetCursorPosY(mixerCursorY);
            ImGui::SetCursorPosX(menuWidth - ImGui::CalcTextSize("LNA gain").x - ImGui::GetFrameHeight() - style.ItemInnerSpacing.x * 2);
        }

        SmGui::LeftLabel("LNA gain");
        if (SmGui::Checkbox(CONCAT("##_mirisdr_lnagain_", _this->name), &_this->gain_lna)) {
            if (_this->running) {
                if (mirisdr_set_lna_gain(_this->openDev, _this->gain_lna)) {
                    flog::error("Could not set Mirisdr gain {0}", _this->selectedSerial);
                }
            }
            shouldUpdateGains = true;
        }

        SmGui::LeftLabel("Mixbuffer gain");
        SmGui::FillWidth();
        float tmp_mbGain = _this->gain_mb;
        if (SmGui::SliderFloatWithSteps(CONCAT("##_mirisdr_mixbgain_", _this->name), &tmp_mbGain, 0, 18, 6, SmGui::FMT_STR_FLOAT_NO_DECIMAL)) {
            _this->gain_mb = tmp_mbGain;
            if (_this->running) {
                if (mirisdr_set_mixbuffer_gain(_this->openDev, _this->gain_mb)) {
                    flog::error("Could not set Mirisdr gain {0}", _this->selectedSerial);
                }
            }
            shouldUpdateGains = true;
        }

        SmGui::LeftLabel("BB gain");
        SmGui::FillWidth();
        if (SmGui::SliderInt(CONCAT("##_mirisdr_bbgain_", _this->name), &_this->gain_bb, 0, 59)) {
            if (_this->running) {
                if (mirisdr_set_baseband_gain(_this->openDev, _this->gain_bb)) {
                    flog::error("Could not set Mirisdr gain {0}", _this->selectedSerial);
                }
            }
            shouldUpdateGains = true;
        }

        if (shouldUpdateGains) {
            _this->updateGains();
        }

        SmGui::FillWidth();
        if (SmGui::Checkbox(CONCAT("Bias-T##_mirisdr_biast_", _this->name), &_this->biasTee)) {
            if (_this->running) {
                mirisdr_set_bias(_this->openDev, _this->biasTee ? 1 : 0);
                _this->biasTee = mirisdr_get_bias(_this->openDev) > 0;
            }
            config.acquire();
            config.conf["devices"][_this->selectedSerial]["biasTee"] = _this->biasTee;
            config.release(true);
        }
    }

    void updateGains() {
        if (this->running) {
            this->gain_mixer = mirisdr_get_mixer_gain(this->openDev) > 0;
            this->gain_lna = mirisdr_get_lna_gain(this->openDev) > 0;
            this->gain_mb = mirisdr_get_mixbuffer_gain(this->openDev);
            this->gain_bb = mirisdr_get_baseband_gain(this->openDev);
        }

        config.acquire();
        config.conf["devices"][this->selectedSerial]["gain_mixer"] = this->gain_mixer;
        config.conf["devices"][this->selectedSerial]["gain_lna"] = this->gain_lna;
        config.conf["devices"][this->selectedSerial]["gain_mb"] = this->gain_mb;
        config.conf["devices"][this->selectedSerial]["gain_bb"] = this->gain_bb;
        config.release(true);

    }

    static void callback(unsigned char *buf, uint32_t len, void *ctx) {
        MirisdrSourceModule* _this = (MirisdrSourceModule*)ctx;
        int count = (len/sizeof(int16_t)) / 2;
        int16_t* buffer = (int16_t*)buf;
        volk_16i_s32f_convert_32f((float*)_this->stream.writeBuf, buffer, 32768.0f, count * 2);
        if (!_this->stream.swap(count)) { return; }
    }

    std::string name;
    mirisdr_dev_t* openDev;
    bool enabled = true;
    std::thread workerThread;
    dsp::stream<dsp::complex_t> stream;
    int sampleRate;
    SourceManager::SourceHandler handler;
    bool running = false;
    double freq;
    std::string selectedSerial = "";
    int devId = 0;
    int srId = 0;
    int bwId = 16;

    bool gain_mixer = 0;
    bool gain_lna = 0;
    int gain_mb = 0;
    int gain_bb = 0;
    bool biasTee = false;

    // TODO: Implement SetCursorPosX etc. in SmGui
    bool serverMode = (bool)core::args["server"];

    std::vector<std::string> devList;
    std::vector<int> devIdxList;
    std::string devListTxt;

#ifdef __ANDROID__
    int devFd = -1;
#endif

};

MOD_EXPORT void _INIT_() {
    json def = json({});
    def["devices"] = json({});
    def["device"] = "";
    config.setPath(core::args["root"].s() + "/mirisdr_config.json");
    config.load(def);
    config.enableAutoSave();
}

MOD_EXPORT ModuleManager::Instance* _CREATE_INSTANCE_(std::string name) {
    return new MirisdrSourceModule(name);
}

MOD_EXPORT void _DELETE_INSTANCE_(ModuleManager::Instance* instance) {
    delete (MirisdrSourceModule*)instance;
}

MOD_EXPORT void _END_() {
    config.disableAutoSave();
    config.save();
}
