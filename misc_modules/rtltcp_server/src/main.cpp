#include <utils/networking.h>
#include <imgui.h>
#include <config.h>
#include <core.h>
#include <gui/style.h>
#include <gui/gui.h>
#include <signal_path/signal_path.h>
#include <module.h>
#include <fstream>
#include <cerrno>
#include <clocale>

#include <dsp/demod/psk.h>
#include <dsp/buffer/packer.h>
#include <dsp/routing/splitter.h>
#include <dsp/stream.h>
#include <dsp/sink/handler_sink.h>

#define CONCAT(a, b)    ((std::string(a) + b).c_str())


SDRPP_MOD_INFO {
    /* Name:            */ "rtltcp_server",
    /* Description:     */ "RTL-TCP server emulation.",
    /* Author:          */ "ericek111",
    /* Version:         */ 0, 0, 1,
    /* Max instances    */ -1
};

ConfigManager config;

class RTLTCPServerModule : public ModuleManager::Instance {
public:
    RTLTCPServerModule(std::string name) {
        this->name = name;

        writeBuffer = new uint8_t[writeBufSize];

        // Load config
        config.acquire();
        bool created = false;
        if (!config.conf.contains(name)) {
            config.conf[name]["host"] = "localhost";
            config.conf[name]["port"] = port;
            created = true;
        }

        std::string host = config.conf[name]["host"];
        strcpy(hostname, host.c_str());
        port = config.conf[name]["port"];
        config.release(created);

        vfo = sigpath::vfoManager.createVFO(name, ImGui::WaterfallVFO::REF_CENTER, 0, 2400000, 2400000 /*sample rate*/, 0, 0, false);
        packer.init(vfo->output, writeBufSize / 2 / sizeof(writeBuffer[0]));
        hnd.init(&packer.out, _vfoSinkHandler, this);

        this->enable();

        gui::menu.registerEntry(name, menuHandler, this, this);
    }

    ~RTLTCPServerModule() {
        if (this->isEnabled()) {
            disable();
        }
        gui::menu.removeEntry(name);
    }

    void postInit() {}

    void enable() {
        if (!this->startServer()) {
            enabled = false;
            return;
        }
        vfo = sigpath::vfoManager.createVFO(name, ImGui::WaterfallVFO::REF_CENTER, 0, 2400000, 2400000 /*sample rate*/, 0, 0, false);
//        vfoSink.init(vfo->output, _vfoSinkHandler, this);
        // flog::info("data: {} {}", sizeof(writeBuffer), sizeof(writeBuffer[0]));
        // vfoSink.start();
        packer.setInput(vfo->output);
        packer.start();
        hnd.start();
        enabled = true;
    }

    void disable() {
        this->stopServer();
        hnd.stop();
        packer.stop();
        // vfoSink.stop();
        sigpath::vfoManager.deleteVFO(vfo);
        enabled = false;
    }

    bool isEnabled() {
        return enabled;
    }

private:

    static void menuHandler(void* ctx) {
        RTLTCPServerModule* _this = (RTLTCPServerModule*)ctx;
        float menuWidth = ImGui::GetContentRegionAvail().x;

        bool listening = (_this->listener && _this->listener->isListening());

        if (listening) { style::beginDisabled(); }
        if (ImGui::InputText(CONCAT("##_rtltcp_server_host_", _this->name), _this->hostname, sizeof(_this->hostname) - 1)) {
            config.acquire();
            config.conf[_this->name]["host"] = std::string(_this->hostname);
            config.release(true);
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
        if (ImGui::InputInt(CONCAT("##_rtltcp_server_port_", _this->name), &_this->port, 0, 0)) {
            config.acquire();
            config.conf[_this->name]["port"] = _this->port;
            config.release(true);
        }
        if (listening) { style::endDisabled(); }

        ImGui::TextUnformatted("Status:");
        ImGui::SameLine();
        if (_this->client && _this->client->isOpen()) {
            ImGui::TextColored(ImVec4(0.0, 1.0, 0.0, 1.0), "Connected");
        }
        else if (listening) {
            ImGui::TextColored(ImVec4(1.0, 1.0, 0.0, 1.0), "Listening");
        }
        else {
            ImGui::TextUnformatted("Idle");
        }
    }

    bool startServer() {
        try {
            listener = net::listen(hostname, port);
            listener->acceptAsync(clientHandler, this);
            return true;
        }
        catch(const std::runtime_error& re) {
            flog::error("Could not start RTL-TCP server listener: {}: {}", re.what(), std::strerror(errno));
        }
        catch (std::exception e) {
            flog::error("Could not start RTL-TCP server: {}", e.what());
        }

        return false;
    }

    void stopServer() {
        try {
            if (client) { client->close(); }
            if (listener) { listener->close(); }
        }
        catch (std::exception e) {
            flog::error("Could not stop RTL-TCP server: {0}", e.what());
        }
    }

    static void clientHandler(net::Conn _client, void* ctx) {
        RTLTCPServerModule* _this = (RTLTCPServerModule*)ctx;
        flog::info("New client!");

        _this->client = std::move(_client);
        uint8_t dongleInfo[12] = {'R', 'T', 'L', '0', 0}; // to not confuse some clients
        _this->client->writeAsync(sizeof(dongleInfo), dongleInfo);
        _this->client->readAsync(5, _this->cmdBuf, dataHandler, _this, false);
        _this->client->waitForEnd();
        _this->client->close();

        flog::info("Client disconnected!");

        _this->listener->acceptAsync(clientHandler, _this);
    }

    static void dataHandler(int count, uint8_t* data, void* ctx) {
        RTLTCPServerModule* _this = (RTLTCPServerModule*)ctx;
        if (count < 5) {
            return;
        }
        auto arg = ntohl(*reinterpret_cast<uint32_t*>(data + 1));
        _this->commandHandler(data[0], arg);
        _this->client->readAsync(5, _this->cmdBuf, dataHandler, _this, false);
    }

    void commandHandler(uint8_t cmd, uint32_t arg) {
        // flog::info("Command: {}   {}", (int) cmd, (int) arg);
        std::lock_guard lck(vfoMtx);
        if (cmd == 1) {
            tuner::tune(tuner::TUNER_MODE_NORMAL, name, arg);
        } else if (cmd == 2) {
            vfo->setSampleRate(arg, arg);
        } else {
            // no-op
        }
    }

    static void _vfoSinkHandler(dsp::complex_t* data, int count, void* ctx) {
        // flog::info("count: {}", count);
        RTLTCPServerModule* _this = (RTLTCPServerModule*)ctx;
        if (!_this->client || !_this->client->isOpen()) {
            return;
        }

        for (int i = 0; i < count; i++) {
            _this->writeBuffer[(2 * i)] = (data[i].re * 127.0f) + 128.0f;
            _this->writeBuffer[(2 * i) + 1] = (data[i].im * 127.0f) + 128.0f;
        }
        _this->client->writeAsync(2 * count * sizeof(_this->writeBuffer[0]), _this->writeBuffer);

    }

    std::string name;
    bool enabled = true;
    char hostname[1024];
    int port = 12500;
    uint8_t cmdBuf[5];
    net::Listener listener;
    net::Conn client;

    VFOManager::VFO* vfo;
    dsp::sink::Handler<dsp::complex_t> vfoSink;
    std::mutex vfoMtx;
    std::atomic<bool> shouldStream = false;
    uint8_t* writeBuffer;
    dsp::buffer::Packer<dsp::complex_t> packer;
    dsp::sink::Handler<dsp::complex_t> hnd;
    const size_t writeBufSize = 2 * 35536 * 8;

};

MOD_EXPORT void _INIT_() {
    std::string root = (std::string)core::args["root"];
    json def = json({});
    config.setPath(root + "/rtl_tcp_server_config.json");
    config.load(def);
    config.enableAutoSave();
}

MOD_EXPORT ModuleManager::Instance* _CREATE_INSTANCE_(std::string name) {
    return new RTLTCPServerModule(name);
}

MOD_EXPORT void _DELETE_INSTANCE_(void* instance) {
    delete (RTLTCPServerModule*)instance;
}

MOD_EXPORT void _END_() {
    config.disableAutoSave();
    config.save();
}
