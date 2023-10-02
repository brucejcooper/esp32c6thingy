#!/usr/bin/env python3
import json
import jinja2

templateLoader = jinja2.FileSystemLoader(searchpath="./")
templateEnv = jinja2.Environment(loader=templateLoader)
header_template = templateEnv.get_template("interface.h.j2")
functions_template = templateEnv.get_template("interface.c.j2")


def write_interface(iface, interface_index):
    name = iface['name']
    nameupper = name.upper()
    iface['index'] = interface_index
    with open("main/interface_{}.h".format(name), "w") as f:
        f.write(header_template.render(iface)) 
    with open("main/interface_{}.c".format(name), "w") as f:
        f.write(functions_template.render(iface)) 

with open('interface.json') as fp:
    interfaces = json.load(fp)
    for idx, iface in enumerate(interfaces):
        print(iface['name'], idx)
        write_interface(iface, idx)
