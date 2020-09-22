"use strict";

class Client {
  constructor() {
    this.onproperty = undefined
    this.onopen = undefined
    this.onerror = undefined
    this.__ws = undefined
  }

  connect() {
    if (this.__ws) {
      this.__ws.close()
    }

    this.__ws = new WebSocket(`ws://${window.location.host}/ws`)

    this.__ws.onopen = () => {
      if (this.onopen) {
        this.onopen()
      }
    }

    this.__ws.onmessage = message => {
      var msg = JSON.parse(message.data)

      switch (msg.msg) {
        case 'property':
          this.onproperty(msg)
          break
      }
    }

    this.__ws.onerror = (error) => {
      console.log(`websocket error: ${error.message}`)
      if (this.onerror) {
        this.onerror()
      }
    }
  }

  stream(state) {
    this.__sendRequest('stream', { state: state })
  }

  answer(sdp) {
    this.__sendRequest('answer', { sdp: sdp })
  }

  candidate(candidate) {
    this.__sendRequest('candidate', {candidate: candidate.toJSON()})
  }

  __sendRequest(type, args) {
    var request = Object.assign({msg: type}, args)

    this.__ws.send(JSON.stringify(request))
  }
}

export class AdaptorDemo {
  constructor() {
    var streamToggle = document.getElementById("stream_toggle")
    streamToggle.onchange = () => {
      this.__signaling.stream (streamToggle.checked)
    }

    this.__signaling = new Client()
    this.__signaling.onproperty = msg => {
      document.getElementById(msg.name).innerText = msg.value
    }
    this.__signaling.connect()
  }
}
