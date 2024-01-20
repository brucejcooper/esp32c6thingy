import { useEffect, useState } from "react";
import Card from "@mui/material/Card";
import CardContent from "@mui/material/CardContent";
import { Alert, Backdrop, CardHeader, CircularProgress, IconButton, Snackbar, styled } from "@mui/material";

import { Save as SaveIcon, Recycling as ReloadIcon } from "@mui/icons-material";
import CodeMirror, { ViewPlugin, ViewUpdate } from "@uiw/react-codemirror";
import { StreamLanguage } from "@codemirror/language";
import { lua } from "@codemirror/legacy-modes/mode/lua";

import React from "react";

interface FileEditorProps {
    ip: string;
    fname: string;
}

const CardContentNoPadding = styled(CardContent)(`
  padding: 0;
  &:last-child {
    padding-bottom: 0;
  }
`);

export function FileEditor(props: FileEditorProps) {
    const [content, setContent] = useState<string | undefined>(undefined);
    const { ip, fname } = props;
    const [error, setError] = useState<Error | undefined>(undefined);
    const [dirty, setDirty] = useState(false);
    const [loading, setLoading] = useState(false);

    useEffect(() => {
        setLoading(true);
        const aborter = new AbortController();
        const signal = aborter.signal;
        const fetcher = async () => {
            try {
                setContent("");
                setError(undefined);
                const res = await fetch(`http://chipgw.8bitcloud.com:8080/${ip}/fs/${fname}`, { signal });
                if (!signal.aborted) {
                    if (res.status > 299) {
                        throw new Error(`File load failed (${res.status}): ${await res.text()}`);
                    }
                    const jdata = await res.json();
                    if (!signal.aborted) {
                        setContent(jdata);
                        setDirty(false);
                    }
                }
            } catch (ex: any) {
                console.log("Fail", ex);
                if (!signal.aborted) {
                    setError(ex);
                }
            } finally {
                if (!signal.aborted) {
                    setLoading(false);
                }
            }
        };

        fetcher();
        return () => {
            aborter.abort();
        };
    }, [ip, fname]);

    const handleSave = (event: React.MouseEvent<HTMLButtonElement>) => {};
    const handleReload = (event: React.MouseEvent<HTMLButtonElement>) => {};

    const onChange = React.useCallback((val: string, _viewUpdate: ViewUpdate) => {
        if (!loading) {
            setContent(val);
            setDirty(true);
        }
    }, []);

    return (
        <div style={{ position: "relative" }}>
            <div
                style={{
                    display: loading ? "flex" : "none",
                    position: "absolute",
                    width: "100%",
                    height: "100%",
                    backgroundColor: "rgba(0,0,0,0.1)",
                    zIndex: 100,
                    flexDirection: "column",
                    alignItems: "center",
                    justifyContent: "center",
                }}
            >
                <CircularProgress />
            </div>
            {error ? (
                <Alert severity="error">{error.message}</Alert>
            ) : (
                <Card>
                    <CardHeader
                        title={`${fname}`}
                        sx={{ paddingBottom: 0.5 }}
                        action={
                            <div>
                                <IconButton aria-label="reset" aria-haspopup="true" onClick={handleSave} disabled={!dirty}>
                                    <SaveIcon />
                                </IconButton>
                                <IconButton aria-label="reset" aria-haspopup="true" onClick={handleReload}>
                                    <ReloadIcon />
                                </IconButton>
                            </div>
                        }
                    />
                    <CardContentNoPadding>
                        <CodeMirror value={content} height="600px" extensions={[StreamLanguage.define(lua)]} onChange={onChange} />
                    </CardContentNoPadding>
                </Card>
            )}
        </div>
    );
}
