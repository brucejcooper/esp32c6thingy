import { MainLayout } from "../layouts/main_layout";
import { useParams } from "react-router-dom";
import { DeviceFiles } from "../components/device_files";
import Grid from "@mui/material/Unstable_Grid2"; // Grid version 2
import { FileEditor } from "../components/file_editor";
import { IconButton, Typography } from "@mui/material";
import { DeviceInfoSmall } from "../components/device_info_small";
import { Recycling as ResetIcon } from "@mui/icons-material";
import { DeviceEvent, DeviceInfo, DeviceModel } from "../data/device";
import { useEffect, useState } from "react";

export function FileRoute() {
    const params = useParams();
    const ip = params.ip!;
    const fname = params.fname!;

    const [device, setDevice] = useState<DeviceInfo>();

    useEffect(() => {
        const dev = new DeviceModel(ip, 15000);
        const updater = (evt: DeviceEvent) => {
            setDevice(evt.device);
        };

        dev.addEventListener("new_info", updater);
        setDevice({
            firmware: dev.firmware,
            heap: dev.heap,
            mac_address: dev.mac_address,
            uptime: dev.uptime,
            reset_reason: dev.reset_reason,
        });
        return () => {
            dev.removeEventListener("new_info", updater);
        };
    }, [ip]);

    async function doReset() {
        try {
            console.log("Sending reset command");
            const res = await fetch(`http://chipgw.8bitcloud.com:8080/${ip}/restart`, {
                method: "POST",
            });
            if (res.status >= 300) {
                throw new Error(`Didn't work: ${res.status}: ${await res.text()}`);
            }
        } catch (ex: any) {
            alert(ex.toString());
        }
    }

    if (!device) {
        return <div>...</div>;
    }

    return (
        <MainLayout title="Device Info">
            <Typography variant="h4">Device {ip} </Typography>
            <IconButton onClick={doReset}>
                <ResetIcon />
            </IconButton>

            <Grid container spacing={1}>
                <Grid xs={12}>
                    <DeviceInfoSmall device={device} />
                </Grid>
                <Grid xs={3}>
                    <DeviceFiles ip={ip} selected={fname} />
                </Grid>
                <Grid xs={9}>
                    <FileEditor ip={ip} fname={fname} />
                </Grid>
            </Grid>
        </MainLayout>
    );
}
