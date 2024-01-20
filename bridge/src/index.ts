
import express from 'express'
import { engine, create } from 'express-handlebars';
import { request as request_coap, IncomingMessage } from 'coap'
import { encode, decodeFirstSync } from 'cbor'
import * as helpers from "./helpers"


const hbs = create({
  helpers,
  partialsDir: [
    "shared/templates/",
    "views/partials/",
  ],
})

const app = express()
app.engine('handlebars', hbs.engine);
app.set('view engine', 'handlebars');
app.set('views', './views');

const port = 8080

interface CoapResponse<T> {
  code: string;
  format: string;
  payload: T;
}

class InvalidResponseError extends Error {
  packet: CoapResponse<any>;
  constructor(pkt: CoapResponse<any>) {
    super("Error making call")
    this.packet = pkt;
  }
}

function mapToString(val: string | number | Buffer | Buffer[] | null | undefined) {
  if (val === null || val === undefined) {
    return undefined;
  }
  return val.toString();
}

const statusMap: Record<string, number> = {
  '2.05': 200,
  '2.03': 204,
  '2.04': 204,

  '4.02': 400,
}

type ReqMethod = "GET" | "PUT" | "POST" | "DELETE";

interface CoapProps {
  ip: string,
  path: string,
  method?: ReqMethod,
}

function callCoAP<T>(props: CoapProps) {
  return new Promise<CoapResponse<any>>((resolve, reject) => {
    const t1 = Date.now()
    const creq = request_coap({
      hostname: props.ip,
      pathname: props.path,
      method: props.method ?? 'GET'
    });
    creq.on('response', (cres: IncomingMessage) => {
      const code = cres.code;
      const format: string = mapToString(cres.headers['Content-Format']) ?? "text/plain"
      const bufs: Buffer[] = []

      cres.on('data', (d: Buffer) => bufs.push(d));
      cres.on('end', () => {
        const d2 = Date.now()
        try {
          const allData = Buffer.concat(bufs)
          let payload: any;
          if (format == "application/cbor") {
            payload = decodeFirstSync(allData)
          } else {
            payload = allData.toString('utf-8');
          }
          console.log("Fetch took", (d2 - t1))
          if (code.startsWith('2')) {
            resolve({ code, format, payload })
          } else {
            reject(new InvalidResponseError({ code, format, payload }))
          }
        } catch (ex) {
          reject(ex);
        }
      });
    });
    creq.on('error', (err) => {
      reject(err);
    })
    creq.end()
  })
}

app.all('/:deviceip/*', async (req, res) => {
  try {
    const ip = req.params.deviceip
    const path = (req.params as Record<string, string>)['0']
    console.log("Got request to", ip, path)

    const cres = await callCoAP({
      ip,
      path,
      method: req.method.toUpperCase() as ReqMethod
    });

    let contentCode: number | undefined = statusMap[cres.code]
    if (!contentCode) {
      const bits = cres.code.split(".")
      contentCode = parseInt(bits[0]) * 100 + parseInt(bits[1])
    }
    res.status(contentCode);
    switch (cres.format) {
      case 'application/cbor':
        res.setHeader('Content-Type', "application/json");
        break;
    }
    res.setHeader('Access-Control-Allow-Origin', '*');
    res.setHeader('Access-Control-Allow-Methods', 'GET, POST, PUT, DELETE');

    res.send(JSON.stringify(cres.payload, function (key, val) {
      // Turn Buffers into their base64 strings.
      const realval = this[key]
      if (Buffer.isBuffer(realval)) {
        return realval.toString('base64')
      }
      return val;
    }))
  } catch (ex) {
    res.status(500);
    res.send("There was an error");
  }
});


app.listen(port, () => {
  console.log(`Example app listening on port ${port}`)
})