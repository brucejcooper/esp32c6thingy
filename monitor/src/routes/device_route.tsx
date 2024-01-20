import { MainLayout } from "../layouts/main_layout";
import { DeviceInfo } from "../components/device_info";
import { useParams } from "react-router-dom";
import { DeviceFiles } from "../components/device_files";
import Grid from "@mui/material/Unstable_Grid2"; // Grid version 2

export function DeviceRoute() {
    const params = useParams();
    const ip = params.ip!;

    return (
        <MainLayout title="Device Info">
            <Grid container spacing={2}>
                <Grid xs={12}>
                    <DeviceInfo ip={ip} />
                </Grid>

                <Grid xs={12}>
                    <DeviceFiles ip={ip} />
                </Grid>
            </Grid>
        </MainLayout>
    );
}
