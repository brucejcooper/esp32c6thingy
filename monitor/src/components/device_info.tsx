import { useEffect, useState } from "react";
import { Buffer } from "buffer";
import Card from "@mui/material/Card";
import CardContent from "@mui/material/CardContent";
import moment from "moment";
import { CardHeader, IconButton, Menu, MenuItem, Stack, Typography } from "@mui/material";
import Grid from "@mui/material/Unstable_Grid2"; // Grid version 2
import { styled } from "@mui/material/styles";

import { MoreVert as MoreVertIcon } from "@mui/icons-material";
import { ResetReason } from "../data/reset_reason";

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

interface FirmwareVersion {
    project: string;
    date: string;
    time: string;
    idf_ver: string;
    sha256: string;
    version: string;
}

interface HeapInfo {
    free: number;
    min_free: number;
    total_allocated: number;
}

interface DeviceDetails {
    uptime: number;
    reset_reason: ResetReason;
    firmware: FirmwareVersion;
    heap: HeapInfo;
    mac_address: string;
}

interface DeviceInfoProps {
    ip: string;
}

export function DeviceInfo(props: DeviceInfoProps) {
    const [data, setData] = useState<DeviceDetails | undefined>(undefined);
    const { ip } = props;
    const [anchorEl, setAnchorEl] = useState<null | HTMLElement>(null);

    useEffect(() => {
        const fetcher = async () => {
            const res = await fetch(`http://chipgw.8bitcloud.com:8080/${ip}/info`);
            const jdata = await res.json();
            setData(jdata);
        };

        fetcher();
    }, [ip]);

    const handleClick = (event: React.MouseEvent<HTMLButtonElement>) => {
        setAnchorEl(event.currentTarget);
    };
    const handleClose = () => {
        setAnchorEl(null);
    };

    return (
        <div>
            {data ? (
                <Card>
                    <CardHeader
                        title={`Device ${ip}`}
                        action={
                            <div>
                                <IconButton
                                    aria-label="reset"
                                    aria-controls={anchorEl ? "open-menu" : undefined}
                                    aria-haspopup="true"
                                    aria-expanded={anchorEl ? "true" : undefined}
                                    onClick={handleClick}
                                >
                                    <MoreVertIcon />
                                </IconButton>
                                <Menu
                                    id="basic-menu"
                                    anchorEl={anchorEl}
                                    open={anchorEl != null}
                                    onClose={handleClose}
                                    MenuListProps={{
                                        "aria-labelledby": "basic-button",
                                    }}
                                >
                                    <MenuItem onClick={handleClose}>Reset</MenuItem>
                                </Menu>
                            </div>
                        }
                    />
                    <CardContent>
                        <Grid container spacing={2}>
                            <Grid xs={4}>
                                <Item>
                                    <Key>MAC</Key>
                                    <Value>{Buffer.from(data.mac_address, "base64").toString("hex")}</Value>
                                </Item>
                            </Grid>
                            <Grid xs={4}>
                                <Item>
                                    <Key>uptime</Key>
                                    <Value>{moment.duration(data.uptime * 1000).humanize()}</Value>
                                </Item>
                            </Grid>
                            <Grid xs={6}>
                                <Typography variant="h6">Firmware</Typography>
                                <Stack>
                                    <Item>
                                        <Key>Project</Key>
                                        <Value>{data.firmware.project}</Value>
                                    </Item>
                                    <Item>
                                        <Key>Version</Key>
                                        <Value>{data.firmware.version}</Value>
                                    </Item>
                                    <Item>
                                        <Key>Build Timestamp</Key>
                                        <Value>
                                            {data.firmware.date} {data.firmware.time}
                                        </Value>
                                    </Item>
                                    <Item>
                                        <Key>Sha256</Key>
                                        <Value>{data.firmware.sha256}</Value>
                                    </Item>
                                    <Item>
                                        <Key>IDF Version</Key>
                                        <Value>{data.firmware.idf_ver}</Value>
                                    </Item>
                                </Stack>
                            </Grid>
                            <Grid xs={6}>
                                <Typography variant="h6">Heap</Typography>
                                <Stack>
                                    <Item>
                                        <Key>allocated</Key>
                                        <Value>{data.heap.total_allocated} kb</Value>
                                    </Item>
                                    <Item>
                                        <Key>free</Key>
                                        <Value>{data.heap.free} kb</Value>
                                    </Item>
                                    <Item>
                                        <Key>min free</Key>
                                        <Value>{data.heap.min_free} kb</Value>
                                    </Item>
                                </Stack>
                            </Grid>
                        </Grid>
                    </CardContent>
                </Card>
            ) : (
                <div>Loading...</div>
            )}
        </div>
    );
}
