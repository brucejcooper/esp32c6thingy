export type ResetReason =
    | "power_on"
    | "software"
    | "deep_sleep"
    | "mwdt0"
    | "rtc_wdt"
    | "cpu0_mwdt0"
    | "cpu0_software"
    | "cpu0_rtc"
    | "brown_out"
    | "rtc_wdt"
    | "super_wdt"
    | "clock glitch"
    | "efuse crc"
    | "jtag";

export interface DeviceFile {
    filename: string;
    etag: string;
}

export interface FirmwareVersion {
    project: string;
    date: string;
    time: string;
    idf_ver: string;
    sha256: string;
    version: string;
}

export interface HeapInfo {
    free: number;
    min_free: number;
    total_allocated: number;
}

export interface DeviceInfo {
    uptime: number;
    reset_reason: ResetReason;
    firmware: FirmwareVersion;
    heap: HeapInfo;
    mac_address: string;
}

export type EventType = "new_info";

export interface DeviceEvent {
    type: EventType;
    device: DeviceModel;
}

export type EventHandler = (evt: DeviceEvent) => void;

export class DeviceModel implements DeviceInfo {
    ip: string;
    files: DeviceFile[] = [];

    uptime: number = 0;
    reset_reason: ResetReason = "power_on";
    firmware: FirmwareVersion = { project: "unknown", date: "", time: "", idf_ver: "unknown", sha256: "", version: "" };
    heap: HeapInfo = { free: 0, min_free: 0, total_allocated: 0 };
    mac_address: string = "";

    private on_new_info_handlers: EventHandler[] = [];
    private info_update_timer: ReturnType<typeof setTimeout> | undefined;
    private updatePeriod: number;
    private currentFetch: AbortController | undefined;

    constructor(ip: string, updatePeriod: number = 10000) {
        this.ip = ip;
        console.log("Created new device for ip", this.ip);

        this.updatePeriod = updatePeriod;
        this.info_update_timer = updatePeriod >= 0 ? setTimeout(this.fetchInfo, 0) : undefined;
    }

    makeUrl(path: string) {
        return `http://chipgw.8bitcloud.com:8080/${this.ip}/${path}`;
    }

    fetchInfo = async () => {
        try {
            if (this.currentFetch) {
                this.currentFetch.abort();
            }
            console.log("fetching info on device", this.ip);
            this.currentFetch = new AbortController();
            const res = await fetch(this.makeUrl("info"), { signal: this.currentFetch.signal });

            if (!this.currentFetch.signal.aborted && res.status == 200) {
                const data: DeviceInfo = await res.json();

                console.log("Data", data);

                Object.assign(this, data);
                this.fireEvent("new_info");
            } else {
                throw new Error(`Could not fetch info: ${res.status} ${await res.text()}`);
            }
        } catch (ex) {
            console.error(ex);
        } finally {
            // Go again - We set a new timer so that we take into consideration how long the fetch takes, which might be slow.
            this.info_update_timer = setTimeout(this.fetchInfo, this.updatePeriod);
        }
    };

    private fireEvent(evt: EventType) {
        for (let l of this.on_new_info_handlers) {
            try {
                l({ type: evt, device: this });
            } catch (ex) {
                console.error("Error calling event handler", ex);
            }
        }
    }

    requestRefresh() {
        this.setUpdatePeriod(this.updatePeriod, 1);
    }

    setUpdatePeriod(period: number, initialPeriod: number = -1) {
        if (initialPeriod == -1) {
            initialPeriod = period;
        }
        this.updatePeriod = period;
        if (this.info_update_timer) {
            clearInterval(this.info_update_timer);
            this.info_update_timer = undefined;
        }
        if (initialPeriod >= 0) {
            this.info_update_timer = setTimeout(this.fetchInfo, initialPeriod);
        }
    }

    addEventListener(evt_type: EventType, handler: EventHandler) {
        switch (evt_type) {
            case "new_info":
                this.on_new_info_handlers.push(handler);
                break;
        }
    }

    removeEventListener(evt_type: EventType, handler: EventHandler) {
        switch (evt_type) {
            case "new_info":
                const idx = this.on_new_info_handlers.indexOf(handler);
                if (idx >= 0) {
                    this.on_new_info_handlers.splice(idx, 1);
                }
                break;
        }
    }
}
