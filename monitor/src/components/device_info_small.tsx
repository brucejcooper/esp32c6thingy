import { useEffect, useState } from "react";
import { Buffer } from "buffer";
import Card from "@mui/material/Card";
import CardContent from "@mui/material/CardContent";
import moment from "moment";
import { CardHeader, IconButton, Menu, MenuItem, Stack, Typography } from "@mui/material";
import Grid from "@mui/material/Unstable_Grid2"; // Grid version 2
import { styled } from "@mui/material/styles";

import { MoreVert as MoreVertIcon } from "@mui/icons-material";
import { DeviceInfo, DeviceModel } from "../data/device";

const Key = styled("div")(({ theme }) => ({
    ...theme.typography.body1,
    // textAlign: 'center',
    color: theme.palette.text.primary,
}));

const Value = styled("div")(({ theme }) => ({
    ...theme.typography.body2,
    // textAlign: 'center',
    color: theme.palette.text.secondary,
}));

const Item = styled("div")(({ theme }) => ({
    ...theme.typography.body2,
    padding: theme.spacing(1),
    // textAlign: 'center',
    color: theme.palette.text.secondary,
}));

function toKb(val: number) {
    return `${(val / 1024).toFixed(2)}Kb`;
}

interface DeviceInfoSmallProps {
    device: DeviceInfo;
}

export function DeviceInfoSmall(props: DeviceInfoSmallProps) {
    const { device } = props;
    const [anchorEl, setAnchorEl] = useState<null | HTMLElement>(null);

    const handleClick = (event: React.MouseEvent<HTMLButtonElement>) => {
        setAnchorEl(event.currentTarget);
    };
    const handleClose = () => {
        setAnchorEl(null);
    };

    return (
        <Stack>
            {device ? (
                <>
                    <Typography variant="body1">
                        {device.firmware.project} v{device.firmware.version} ({device.firmware.sha256}) {device.firmware.time} {device.firmware.date} up{" "}
                        {moment.duration(device.uptime * 1000).humanize()}
                    </Typography>
                    <Typography variant="body2">
                        Heap {toKb(device.heap.free)} free of {toKb(device.heap.total_allocated + device.heap.free)}, min free {toKb(device.heap.min_free)}
                    </Typography>
                </>
            ) : (
                <div>Loading...</div>
            )}
        </Stack>
    );
}
