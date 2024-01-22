#!/usr/bin/env python
import logging
import asyncio
import cbor2
import traceback
from os import listdir
from os.path import isfile, join
import yaml
import argparse

from aiocoap import *
from aiocoap.numbers import GET,PUT,POST
import hashlib

logging.basicConfig(level=logging.INFO)

# SWITCH_DEVICE_ADDR='fdbf:1afc:5480:1:8a08:4ae9:e400:964d'
# DALI_BRIDGE_DEVICE_ADDR='fdbf:1afc:5480:1:30a3:bef2:6c55:fccd'

class CallFailedException(Exception):
    pass

async def call(protocol, ip_addr, path, req_code, payload = ""):
    response = await protocol.request(Message(code=req_code, uri="coap://[{}]/{}".format(ip_addr, path), payload=payload)).response_nonraising
    
    if req_code is Code.GET:
        expected_code = Code.CONTENT
    elif req_code is Code.PUT or req_code is Code.POST:
        expected_code = Code.CHANGED
    else:
        raise Exception("Unknown request code")
    if response.code is not expected_code:
        raise CallFailedException('Result: %s - %r'%(response.code, response.payload))
    if response.payload == b'':
        return None
    # print("Raw bytes is {}".format(response.payload.hex()))
    if response.opt.content_format == ContentFormat.CBOR:
        return cbor2.loads(response.payload)
    else:
        return response.payload.decode('utf-8')


def get_etag(content):
    return hashlib.md5(content).digest()
    
def readfile(f):
    with open(f, 'rb') as f:
        return f.read()
    


async def main(args):
    print("Syncing with args", args.ip)
    protocol = await Context.create_client_context()
    remote_files = (await call(protocol, args.ip, "fs", GET))['files']

    srcpath = "/tmp/espcoap_tmp_img"
    localfiles = [f for f in listdir(srcpath) if isfile(join(srcpath, f))]

    try:
        num_changes = 0
        for idx, fname in enumerate(localfiles):
            if idx == 0:
                remote_file = "init.lua"
            else:
                remote_file = fname

            remote_etag = remote_files.get(remote_file, None)
            content = readfile(join(srcpath,fname))
            local_etag = get_etag(content)

            if remote_etag != local_etag:
                print("Updating changed file {}".format(fname))
                await call(protocol, args.ip, "fs/{}".format(remote_file), PUT, content)
                num_changes = num_changes + 1
            else:
                print("File {} in sync".format(fname))

        if num_changes > 0:
            print("Rebooting")
            await call(protocol, args.ip, "restart", POST)

    except Exception as e:
        print('Failed to Sync', e)
        print(traceback.format_exc())


parser = argparse.ArgumentParser(
                    prog='sync',
                    description='Copies files from local directories onto COAP based devices',
                    epilog='copyright © 2024 mechination.com.au')
parser.add_argument('ip')
asyncio.run(main(parser.parse_args()))
