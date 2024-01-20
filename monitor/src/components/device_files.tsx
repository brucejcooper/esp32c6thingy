import { useEffect, useState } from "react";
import { Card, CardContent, CardHeader, Link, Typography } from "@mui/material";
import { styled } from "@mui/material/styles";
import { Link as RouterLink } from "react-router-dom";

const FileListEl = styled("ol")(({ theme }) => ({
    ...theme.typography.body1,
    color: theme.palette.text.primary,
    listStyle: "none",
    padding: 0,
    margin: 0,
}));

const Hash = styled("span")(({ theme }) => ({
    ...theme.typography.body2,
    color: theme.palette.text.secondary,
}));

type FileList = Record<string, string>;

interface FilesResult {
    files: FileList;
    next?: string;
}

interface FileInfo {
    fname: string;
    etag: string;
}

interface DeviceFilesProps {
    selected?: string | undefined;
    ip: string;
}

export function DeviceFiles(props: DeviceFilesProps) {
    const [data, setData] = useState<FileInfo[] | undefined>();
    const { ip } = props;

    useEffect(() => {
        const aborter = new AbortController();

        const fetcher = async () => {
            const res = await fetch(`http://chipgw.8bitcloud.com:8080/${ip}/fs`, { signal: aborter.signal });
            if (res.status == 200) {
                const jdata = (await res.json()) as FilesResult;
                const f: FileInfo[] = Object.entries(jdata.files).map(([fname, etag]) => ({ fname, etag }));
                f.sort((a, b) => a.fname.localeCompare(b.fname));
                setData(f);
            } else {
                throw new Error("Failed to fetch files");
            }
        };

        fetcher().catch((ex) => {
            console.error(ex);
        });
        return () => {
            aborter.abort();
        };
    }, [ip]);

    const items = (data || []).map(({ fname, etag }) => (
        <li key={fname}>
            {props.selected == fname ? (
                <span>{fname}</span>
            ) : (
                <span>
                    <Link component={RouterLink} to={`/device/${ip}/fs/${encodeURIComponent(fname)}`}>
                        {fname}
                    </Link>
                    {/* = <Hash>{hash}</Hash> */}
                </span>
            )}
        </li>
    ));

    return (
        <div>
            <Card>
                <CardHeader title={"Files"} />
                <CardContent>
                    <FileListEl>{items}</FileListEl>
                </CardContent>
            </Card>
        </div>
    );
}
