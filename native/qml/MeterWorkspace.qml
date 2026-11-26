import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id:workspace
    required property color ch1
    required property color ch2
    required property var modes
    color:"#17212b";radius:12;border.color:"#34424d"
    property var state:pokit.meterTools
    function command(action,channel,options) { pokit.meterCommand(action,channel,options||{}) }
    function valueText(value,unit) {
        if(unit==="Durchgang")return value?"Geschlossen":"Offen"
        const v=unit==="F"?value*1e6:value
        return Number(v).toLocaleString(Qt.locale("de_DE"),'f',4)
    }
    function unitText(unit) { return unit==="F"?"µF":unit }
    function shortValue(value,unit) { return valueText(value,unit)+" "+unitText(unit) }
    function timeText(ms) { return new Date(ms).toLocaleTimeString(Qt.locale("de_DE"),"HH:mm:ss") }

    component ChannelCard: Rectangle {
        id:card
        required property int index
        property var measurement:workspace.state.channels[index]
        readonly property bool online:index===0?pokit.connected:pokit.connected2
        readonly property bool valid:index===0?pokit.meterValid:pokit.meterValid2
        readonly property bool overrange:index===0?pokit.meterOverrange:pokit.meterOverrange2
        readonly property color channelColor:index===0?workspace.ch1:workspace.ch2
        Layout.fillWidth:true;Layout.preferredWidth:1;Layout.minimumWidth:0;Layout.preferredHeight:248
        color:measurement.alarm?"#3b2227":"#101c26";border.color:measurement.alarm?"#ff7878":"#344c5c";radius:8
        ColumnLayout { anchors.fill:parent;anchors.margins:12;spacing:5
            RowLayout { Layout.fillWidth:true
                Label { Layout.fillWidth:true;elide:Text.ElideRight;text:"CH"+(card.index+1)+" · "+(card.index===0?pokit.deviceName:pokit.deviceName2);color:card.channelColor;font.pixelSize:16;font.bold:true }
                Label { text:card.online?"● Verbunden":"○ Offline";color:card.online?"#4ee6a5":"#93a9b8";font.pixelSize:12 }
            }
            RowLayout { Layout.fillWidth:true
                Label { Layout.fillWidth:true;elide:Text.ElideRight;text:card.index===0?pokit.meterSwitch:pokit.meterSwitch2;color:"#9eb0bd";font.pixelSize:12 }
                ComboBox { model:workspace.modes;textRole:"label";valueRole:"key";implicitWidth:190;onActivated:pokit.setMeterMode(card.index+1,currentValue) }
            }
            Label { Layout.fillWidth:true;Layout.preferredHeight:52;horizontalAlignment:Text.AlignHCenter;verticalAlignment:Text.AlignVCenter
                text:card.measurement.hold?workspace.valueText(card.measurement.display,card.measurement.unit):card.overrange?"OL":card.online&&card.valid&&card.measurement.count>0?workspace.valueText(card.measurement.display,card.measurement.unit):"—"
                color:"#edf6fb";font.family:"Consolas";font.pixelSize:42;elide:Text.ElideRight
            }
            Label { Layout.fillWidth:true;horizontalAlignment:Text.AlignHCenter;color:card.measurement.alarm?"#ff9191":"#93b4c6";font.pixelSize:14
                text:workspace.unitText(card.measurement.unit)+(card.measurement.hold?" · HOLD":"")+(card.measurement.relative?" · REL (Ref. "+workspace.shortValue(card.measurement.reference,card.measurement.unit)+")":"")+(card.measurement.alarm<0?" · UNTERGRENZE":card.measurement.alarm>0?" · OBERGRENZE":"")
            }
            RowLayout { Layout.fillWidth:true
                Button { text:"HOLD";checkable:true;checked:card.measurement.hold;enabled:card.measurement.count>0;Layout.fillWidth:true;onClicked:workspace.command("hold",card.index+1) }
                Button { text:"REL";checkable:true;checked:card.measurement.relative;enabled:card.measurement.count>0&&card.measurement.unit!=="Durchgang";Layout.fillWidth:true;onClicked:workspace.command("relative",card.index+1) }
                Button { text:"Reset";Layout.fillWidth:true;onClicked:workspace.command("reset",card.index+1) }
                Button { text:card.measurement.limits?"Grenzen ●":"Grenzen";Layout.fillWidth:true
                    onClicked:{limitsDialog.channel=card.index+1;limitsDialog.unit=card.measurement.unit;lowerInput.text=Number(card.measurement.lower).toString();upperInput.text=Number(card.measurement.upper).toString();limitsEnabled.checked=card.measurement.limits;limitsDialog.open()}
                }
            }
            RowLayout { Layout.fillWidth:true;spacing:12
                Repeater { model:[{name:"Min",key:"minimum"},{name:"Max",key:"maximum"},{name:"Mittelwert",key:"mean"}]
                    ColumnLayout { required property var modelData;Layout.fillWidth:true;Layout.preferredWidth:1;Layout.minimumWidth:0;spacing:2
                        Label { text:modelData.name;color:"#9eb0bd";font.pixelSize:11 }
                        Label { Layout.fillWidth:true;elide:Text.ElideRight;text:card.measurement.count>0?workspace.shortValue(card.measurement[modelData.key],card.measurement.unit):"—";color:"#edf6fb";font.pixelSize:14;font.family:"Consolas" }
                    }
                }
                Label { text:"n = "+card.measurement.count;color:"#9eb0bd";font.pixelSize:11 }
            }
        }
    }

    ColumnLayout { anchors.fill:parent;anchors.margins:14;spacing:8
        RowLayout { Layout.fillWidth:true
            Label { text:"LIVE-MULTIMETER";color:"#8ba0af";font.pixelSize:14;font.bold:true;Layout.fillWidth:true }
            Label { text:"Abtastung";color:"#93a9b8" }
            ComboBox { model:["0,5 s","1 s","2 s"];onActivated:pokit.setMeterInterval([500,1000,2000][currentIndex]) }
        }
        RowLayout { Layout.fillWidth:true;spacing:12;ChannelCard {index:0} ChannelCard {index:1} }
        RowLayout { Layout.fillWidth:true;spacing:8
            Button { text:workspace.state.recording?"■ AUFZEICHNUNG STOPPEN":"● AUFZEICHNEN";onClicked:workspace.command("record",0) }
            Button { text:"PDF-Bericht";onClicked:workspace.command("pdf",0) }
            Button { text:"Ordner / CSV";onClicked:workspace.command("folder",0) }
            Item { Layout.fillWidth:true }
            Label { text:workspace.state.recording?"● CSV · "+workspace.state.rows+" Werte":"CSV-Aufzeichnung bereit";color:workspace.state.recording?"#4ee6a5":"#93a9b8" }
            Button { text:"Ereignisse ("+workspace.state.events.length+")";onClicked:eventsDialog.open() }
        }
        RowLayout { Layout.fillWidth:true
            Label { text:"Zeitfenster";color:"#93a9b8" }
            ComboBox { id:windowBox;model:["30 s","2 min","10 min","1 h","6 h","24 h"];currentIndex:[30,120,600,3600,21600,86400].indexOf(workspace.state.window)
                displayText:currentIndex>=0?currentText:workspace.state.window+" s"
                onActivated:workspace.command("view",0,{seconds:[30,120,600,3600,21600,86400][currentIndex]})
            }
            Button { text:"−";ToolTip.visible:hovered;ToolTip.text:"Weiter herauszoomen";onClicked:workspace.command("view",0,{seconds:workspace.state.window*2}) }
            Button { text:"+";ToolTip.visible:hovered;ToolTip.text:"Weiter hineinzoomen";onClicked:workspace.command("view",0,{seconds:workspace.state.window/2}) }
            Button { text:"◀";ToolTip.visible:hovered;ToolTip.text:"Ältere Werte";onClicked:workspace.command("view",0,{follow:false,offset:workspace.state.offset+workspace.state.window/2}) }
            Button { text:"▶";ToolTip.visible:hovered;ToolTip.text:"Neuere Werte";onClicked:workspace.command("view",0,{follow:false,offset:Math.max(0,workspace.state.offset-workspace.state.window/2)}) }
            Button { text:"LIVE";highlighted:workspace.state.follow;onClicked:workspace.command("view",0,{follow:true}) }
            Label { Layout.fillWidth:true;horizontalAlignment:Text.AlignRight;color:"#93a9b8";text:workspace.timeText(workspace.state.start)+" – "+workspace.timeText(workspace.state.end)+" · "+workspace.state.window+" s" }
        }
        Canvas { id:trend;Layout.fillWidth:true;Layout.fillHeight:true;Layout.minimumHeight:100
            property var snapshot:workspace.state
            onSnapshotChanged:requestPaint()
            onWidthChanged:requestPaint();onHeightChanged:requestPaint()
            onPaint:{
                const c=getContext("2d");c.reset();c.fillStyle="#0b151d";c.fillRect(0,0,width,height)
                const left=110,right=width-12,span=snapshot.end-snapshot.start
                for(let channel=0;channel<2;++channel){
                    const data=snapshot.channels[channel],points=data.plot,top=channel*height/2+15,bottom=(channel+1)*height/2-24
                    let low=Infinity,high=-Infinity
                    for(let i=0;i<points.length;++i){low=Math.min(low,points[i].v);high=Math.max(high,points[i].v)}
                    if(low===high){low-=.5;high+=.5}
                    c.font="12px sans-serif";c.fillStyle=channel===0?workspace.ch1:workspace.ch2;c.fillText("CH"+(channel+1)+" "+workspace.unitText(data.unit),8,top+12)
                    c.strokeStyle="#283e4d";c.lineWidth=1
                    for(let grid=0;grid<=4;++grid){const y=top+grid*(bottom-top)/4;c.beginPath();c.moveTo(left,y);c.lineTo(right,y);c.stroke()}
                    if(points.length===0){c.fillStyle="#93a9b8";c.fillText("Keine Werte in diesem Zeitfenster",left+8,top+22);continue}
                    c.fillStyle="#93a9b8";c.fillText(Number(high).toPrecision(4),8,top+32);c.fillText(Number(low).toPrecision(4),8,bottom)
                    c.strokeStyle=channel===0?workspace.ch1:workspace.ch2;c.lineWidth=2
                    let previous=0
                    for(let i=0;i<points.length;++i){const x=left+(points[i].t-snapshot.start)/span*(right-left),y=bottom-(points[i].v-low)/(high-low)*(bottom-top)
                        if(i===0||points[i].t-previous>5000){c.beginPath();c.moveTo(x,y)}else{c.lineTo(x,y);c.stroke();c.beginPath();c.moveTo(x,y)}previous=points[i].t
                        c.fillStyle=c.strokeStyle;c.fillRect(x-1,y-1,2,2)
                    }
                }
                c.font="11px sans-serif"
                for(let e=0;e<snapshot.events.length;++e){const event=snapshot.events[e]
                    if(event.kind!=="comment"||event.t<snapshot.start||event.t>snapshot.end)continue
                    const x=left+(event.t-snapshot.start)/span*(right-left)
                    c.strokeStyle="#537b90";c.lineWidth=1;c.beginPath();c.moveTo(x,0);c.lineTo(x,height);c.stroke()
                    c.fillStyle="#b6d7e8";c.fillText(event.text.slice(0,30),Math.max(left,Math.min(x+5,width-210)),12)
                }
            }
            MouseArea {anchors.fill:parent;acceptedButtons:Qt.NoButton;onWheel:function(wheel){workspace.command("view",0,{seconds:workspace.state.window*(wheel.angleDelta.y>0?.5:2)});wheel.accepted=true}}
        }
        RowLayout { Layout.fillWidth:true
            TextField { id:comment;Layout.fillWidth:true;placeholderText:"Kommentar / Ereignismarker zur Messung";maximumLength:1000;onAccepted:commentButton.clicked() }
            Button { id:commentButton;text:"Markieren";enabled:comment.text.trim().length>0;onClicked:{workspace.command("comment",0,{text:comment.text});comment.clear()} }
        }
        Label { Layout.fillWidth:true;elide:Text.ElideMiddle;color:"#93a9b8";font.pixelSize:11
            text:workspace.state.message.length?workspace.state.message:"Statistik und CSV: absolute Werte. HOLD / REL: nur Anzeige. Verlauf: letzte 100.000 Werte je Kanal; vollständige Aufzeichnung als CSV."
        }
    }

    Dialog { id:limitsDialog;property int channel:1;property string unit:"";title:"Grenzwerte · CH"+channel;modal:true;anchors.centerIn:parent;width:390
        standardButtons:Dialog.Ok|Dialog.Cancel
        ColumnLayout { anchors.fill:parent
            Label { Layout.fillWidth:true;wrapMode:Text.WordWrap;text:"Absolute Werte in "+limitsDialog.unit+" (bei Kapazität in F). Alarm bei Unter-/Überschreitung; Einträge bei Zustandswechseln." }
            CheckBox { id:limitsEnabled;text:"Grenzwertalarm aktiv" }
            Label {text:"Untergrenze"}
            TextField { id:lowerInput;Layout.fillWidth:true;placeholderText:"Untergrenze";selectByMouse:true }
            Label {text:"Obergrenze"}
            TextField { id:upperInput;Layout.fillWidth:true;placeholderText:"Obergrenze";selectByMouse:true }
        }
        onAccepted:workspace.command("limits",channel,{enabled:limitsEnabled.checked,lower:lowerInput.text.replace(",","."),upper:upperInput.text.replace(",",".")})
    }
    Dialog { id:eventsDialog;title:"Ereignisse und Kommentare · letzte 100";modal:true;anchors.centerIn:parent;width:Math.min(workspace.width-40,850);height:Math.min(workspace.height-30,500);standardButtons:Dialog.Close
        ListView { anchors.fill:parent;clip:true;model:workspace.state.events;spacing:5
            ScrollBar.vertical:ScrollBar {}
            delegate:Label { required property var modelData;width:ListView.view.width;wrapMode:Text.WordWrap;padding:8;color:"#edf6fb";font.pixelSize:13
                text:modelData.time+"  "+(modelData.channel>0?"CH"+modelData.channel+"  ":"")+modelData.text
                background:Rectangle {color:"#17212b";radius:4}
            }
        }
    }
}
