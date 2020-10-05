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

  property(name, value) {
    this.__sendRequest('property', { name: name, value: value })
  }

  __sendRequest(type, args) {
    var request = Object.assign({msg: type}, args)

    this.__ws.send(JSON.stringify(request))
  }
}

export class AdaptorDemo {
  constructor() {
    this.__signaling = new Client()
    this.__signaling.onproperty = msg => {
      var element = document.getElementById(msg.name)
      if (element.type == "checkbox") {
        element.checked = msg.value
      } else {
        element.value = msg.value
      }
      element.dispatchEvent(new Event ('change'))
    }
    this.__signaling.connect()
  }

  property(name, value) {
    this.__signaling.property(name, value)
  }
  
  stream(state) {
    this.__signaling.stream(state)
  }
}
