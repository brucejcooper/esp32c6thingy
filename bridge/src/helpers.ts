import moment from "moment"


export function encode (msg: any, format: BufferEncoding = 'utf-8') {
    if (!(msg instanceof Buffer)) {
        return undefined;
    }
    return msg.toString(format)
}


export function duration(msg: any) {
    if (typeof(msg) !== 'number') {
        return undefined
    }
    const duration = moment.duration(msg * 1000)
    return duration.humanize()

}

export function kilobytes(num: any) {
    if (typeof(num) !== 'number') {
        return undefined
    }
    return (num / 1024.0).toFixed(2)
    
}