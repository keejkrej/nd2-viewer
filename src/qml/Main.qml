import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import Nd2Viewer 1.0

ApplicationWindow {
    id: window
    width: 1440
    height: 920
    visible: true
    title: appController.windowTitle
    color: "#0f1218"

    menuBar: MenuBar {
        Menu {
            title: qsTr("&File")
            Action {
                text: qsTr("&Open...")
                shortcut: StandardKey.Open
                onTriggered: openDialog.open()
            }
            Action {
                text: qsTr("&Reload")
                enabled: appController.hasDocument
                onTriggered: appController.reload()
            }
            MenuSeparator {}
            Action {
                text: qsTr("File &Info...")
                enabled: appController.hasDocument
                onTriggered: fileInfoDialog.open()
            }
            MenuSeparator {}
            Action {
                text: qsTr("Export &Frame...")
                enabled: appController.hasDocument && !appController.volumeViewActive
                onTriggered: saveFrameDialog.open()
            }
            Action {
                text: qsTr("Export &ROI...")
                enabled: appController.hasDocument && !appController.volumeViewActive && imageViewport.roiMode
                onTriggered: saveRoiDialog.open()
            }
            MenuSeparator {}
            Action {
                text: qsTr("&Quit")
                shortcut: StandardKey.Quit
                onTriggered: Qt.quit()
            }
        }
        Menu {
            title: qsTr("&View")
            Action {
                text: appController.viewActionText
                enabled: appController.hasDocument
                onTriggered: appController.volumeViewActive ? volumeViewport.resetView() : imageViewport.zoomToFit()
            }
            Action {
                text: qsTr("Actual Size")
                enabled: appController.hasDocument && !appController.volumeViewActive
                onTriggered: imageViewport.setActualSize()
            }
            MenuSeparator {}
            Action {
                text: qsTr("Draw ROI")
                checkable: true
                checked: imageViewport.roiMode
                enabled: appController.hasDocument && !appController.volumeViewActive
                onTriggered: imageViewport.roiMode = checked
            }
            Action {
                text: qsTr("Clear ROI")
                enabled: appController.hasDocument && !appController.volumeViewActive
                onTriggered: imageViewport.clearRoi()
            }
        }
        Menu {
            title: qsTr("&Tools")
            Action {
                text: qsTr("Auto Contrast All")
                enabled: appController.hasDocument
                onTriggered: appController.autoContrastAll()
            }
            Action {
                text: qsTr("Deconvolution...")
                enabled: appController.hasDocument && !appController.volumeViewActive && !appController.deconvolutionBusy
                onTriggered: deconvolutionDialog.open()
            }
        }
    }

    FileDialog {
        id: openDialog
        title: qsTr("Open microscopy file")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("Microscopy files (*.nd2 *.czi)"), qsTr("All files (*)")]
        onAccepted: appController.openUrl(selectedFile)
    }

    FileDialog {
        id: saveFrameDialog
        title: qsTr("Export current frame")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "png"
        nameFilters: [qsTr("PNG images (*.png)")]
        onAccepted: appController.exportRenderedFrame(selectedFile, false)
    }

    FileDialog {
        id: saveRoiDialog
        title: qsTr("Export ROI")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "png"
        nameFilters: [qsTr("PNG images (*.png)")]
        onAccepted: appController.exportRenderedFrame(selectedFile, true)
    }

    ColorDialog {
        id: colorDialog
        property int channelIndex: -1
        title: qsTr("Channel color")
        onAccepted: appController.setChannelColor(channelIndex, selectedColor)
    }

    Dialog {
        id: fileInfoDialog
        title: qsTr("File Info")
        modal: false
        standardButtons: Dialog.Close
        width: Math.min(window.width - 96, 860)
        height: Math.min(window.height - 96, 680)
        anchors.centerIn: parent
        ScrollView {
            anchors.fill: parent
            TextArea {
                readOnly: true
                wrapMode: TextEdit.NoWrap
                text: appController.fileInfoText
                font.family: "Consolas"
                color: "#d9dde7"
                selectedTextColor: "#10131a"
                selectionColor: "#8ab4ff"
                background: Rectangle { color: "#151922"; radius: 4 }
            }
        }
    }

    Dialog {
        id: deconvolutionDialog
        title: qsTr("Deconvolution")
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel
        anchors.centerIn: parent
        width: 420
        property int iterations: 20
        property real sigma: 1.2
        property int radius: 5
        property bool useRoi: false
        onAccepted: appController.runDeconvolution(iterations, sigma, radius, useRoi)
        GridLayout {
            columns: 2
            rowSpacing: 10
            columnSpacing: 12
            anchors.fill: parent
            Label { text: qsTr("Iterations") }
            SpinBox {
                from: 1
                to: 200
                value: deconvolutionDialog.iterations
                onValueModified: deconvolutionDialog.iterations = value
            }
            Label { text: qsTr("Sigma") }
            TextField {
                text: deconvolutionDialog.sigma.toString()
                inputMethodHints: Qt.ImhFormattedNumbersOnly
                onEditingFinished: deconvolutionDialog.sigma = Number(text)
            }
            Label { text: qsTr("Kernel radius") }
            SpinBox {
                from: 1
                to: 64
                value: deconvolutionDialog.radius
                onValueModified: deconvolutionDialog.radius = value
            }
            CheckBox {
                Layout.columnSpan: 2
                text: qsTr("Use ROI")
                checked: deconvolutionDialog.useRoi
                onToggled: deconvolutionDialog.useRoi = checked
            }
        }
    }

    Dialog {
        id: deconvolutionResult
        title: appController.deconvolutionTitle
        modal: false
        standardButtons: Dialog.Close
        width: Math.min(window.width - 80, 1000)
        height: Math.min(window.height - 80, 760)
        anchors.centerIn: parent
        QuickImageViewport {
            anchors.fill: parent
            controller: appController
            imageRole: "deconvolution"
        }
    }

    Connections {
        target: appController
        function onDeconvolutionStateChanged() {
            if (appController.hasDeconvolutionResult && !appController.deconvolutionBusy)
                deconvolutionResult.open()
        }
        function onErrorTextChanged() {
            if (appController.errorText.length > 0)
                errorDialog.open()
        }
    }

    Dialog {
        id: errorDialog
        title: qsTr("nd2-viewer")
        modal: true
        standardButtons: Dialog.Ok
        anchors.centerIn: parent
        onAccepted: appController.clearError()
        Label {
            width: Math.min(520, window.width - 120)
            wrapMode: Text.WordWrap
            text: appController.errorText
        }
    }

    footer: ToolBar {
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 10
            anchors.rightMargin: 10
            Label {
                Layout.fillWidth: true
                elide: Text.ElideRight
                text: appController.statusText.length > 0 ? appController.statusText : appController.infoText
            }
            BusyIndicator {
                running: appController.busy || appController.deconvolutionBusy
                visible: running
                implicitWidth: 24
                implicitHeight: 24
            }
            Label {
                text: appController.pixelText
                elide: Text.ElideRight
                Layout.maximumWidth: 360
            }
        }
    }

    SplitView {
        anchors.fill: parent
        orientation: Qt.Horizontal

        Pane {
            SplitView.preferredWidth: 330
            SplitView.minimumWidth: 280
            padding: 12
            background: Rectangle { color: "#151922" }

            ColumnLayout {
                anchors.fill: parent
                spacing: 14

                Label {
                    text: appController.infoText
                    color: "#d9dde7"
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }

                GroupBox {
                    Layout.fillWidth: true
                    title: qsTr("Navigation")
                    background: Rectangle { color: "#1b202b"; border.color: "#2a3140"; radius: 6 }
                    ColumnLayout {
                        anchors.fill: parent
                        Repeater {
                            model: appController.loopModel
                            delegate: ColumnLayout {
                                required property int index
                                required property string label
                                required property string details
                                required property int previewValue
                                required property int maximum
                                required property bool isTime
                                required property bool locked
                                Layout.fillWidth: true
                                spacing: 4
                                RowLayout {
                                    Layout.fillWidth: true
                                    Label {
                                        text: label
                                        color: locked ? "#7d8798" : "#d9dde7"
                                        Layout.fillWidth: true
                                    }
                                    ToolButton {
                                        visible: isTime
                                        text: qsTr("Play")
                                        enabled: !locked
                                        onClicked: appController.startPlayback(1)
                                    }
                                    SpinBox {
                                        from: 0
                                        to: maximum
                                        value: previewValue
                                        enabled: !locked
                                        editable: true
                                        onValueModified: appController.commitLoopValue(index, value)
                                    }
                                }
                                Slider {
                                    Layout.fillWidth: true
                                    from: 0
                                    to: maximum
                                    stepSize: 1
                                    value: previewValue
                                    enabled: !locked
                                    onMoved: appController.setLoopPreviewValue(index, Math.round(value))
                                    onPressedChanged: if (!pressed) appController.commitLoopValue(index, Math.round(value))
                                }
                                Label {
                                    text: details
                                    color: "#8c96a8"
                                    font.pixelSize: 11
                                }
                            }
                        }
                    }
                }

                GroupBox {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    title: qsTr("Channels")
                    background: Rectangle { color: "#1b202b"; border.color: "#2a3140"; radius: 6 }
                    ScrollView {
                        anchors.fill: parent
                        ColumnLayout {
                            width: parent.width
                            spacing: 10
                            CheckBox {
                                text: qsTr("Live auto")
                                checked: appController.liveAutoEnabled
                                enabled: appController.hasDocument && !appController.volumeViewActive
                                onToggled: appController.setLiveAutoEnabled(checked)
                            }
                            Button {
                                text: qsTr("Auto all")
                                enabled: appController.hasDocument
                                onClicked: appController.autoContrastAll()
                            }
                            Repeater {
                                model: appController.channelModel
                                delegate: Frame {
                                    required property int index
                                    required property string name
                                    required property bool enabled
                                    required property color color
                                    required property real low
                                    required property real high
                                    required property real lowPercentile
                                    required property real highPercentile
                                    Layout.fillWidth: true
                                    background: Rectangle { color: "#202633"; border.color: "#30394a"; radius: 6 }
                                    ColumnLayout {
                                        anchors.fill: parent
                                        RowLayout {
                                            Layout.fillWidth: true
                                            CheckBox {
                                                checked: enabled
                                                onToggled: appController.setChannelEnabled(index, checked)
                                            }
                                            Rectangle {
                                                width: 22
                                                height: 22
                                                color: model.color
                                                border.color: "#d9dde7"
                                                radius: 3
                                                MouseArea {
                                                    anchors.fill: parent
                                                    onClicked: {
                                                        colorDialog.channelIndex = index
                                                        colorDialog.selectedColor = model.color
                                                        colorDialog.open()
                                                    }
                                                }
                                            }
                                            Label {
                                                text: name
                                                color: "#d9dde7"
                                                elide: Text.ElideRight
                                                Layout.fillWidth: true
                                            }
                                            Button {
                                                text: qsTr("Auto")
                                                onClicked: appController.autoContrastChannel(index)
                                            }
                                        }
                                        RowLayout {
                                            Layout.fillWidth: true
                                            TextField {
                                                Layout.fillWidth: true
                                                text: Number(low).toPrecision(5)
                                                onEditingFinished: appController.setChannelLow(index, Number(text))
                                            }
                                            TextField {
                                                Layout.fillWidth: true
                                                text: Number(high).toPrecision(5)
                                                onEditingFinished: appController.setChannelHigh(index, Number(text))
                                            }
                                        }
                                        RowLayout {
                                            Layout.fillWidth: true
                                            TextField {
                                                Layout.fillWidth: true
                                                text: Number(lowPercentile).toFixed(2)
                                                onEditingFinished: appController.setChannelPercentiles(index, Number(text), highPercentile)
                                            }
                                            TextField {
                                                Layout.fillWidth: true
                                                text: Number(highPercentile).toFixed(2)
                                                onEditingFinished: appController.setChannelPercentiles(index, lowPercentile, Number(text))
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        Pane {
            SplitView.fillWidth: true
            padding: 0
            background: Rectangle { color: "#0f1218" }

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                ToolBar {
                    Layout.fillWidth: true
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 8
                        Button {
                            text: qsTr("2D")
                            checkable: true
                            checked: !appController.volumeViewActive
                            enabled: appController.hasDocument
                            onClicked: appController.volumeViewActive = false
                        }
                        Button {
                            text: qsTr("3D")
                            checkable: true
                            checked: appController.volumeViewActive
                            enabled: appController.volumeAvailable
                            onClicked: appController.volumeViewActive = true
                        }
                        ToolSeparator {}
                        Button {
                            text: appController.viewActionText
                            enabled: appController.hasDocument
                            onClicked: appController.volumeViewActive ? volumeViewport.resetView() : imageViewport.zoomToFit()
                        }
                        Button {
                            text: qsTr("1:1")
                            enabled: appController.hasDocument && !appController.volumeViewActive
                            onClicked: imageViewport.setActualSize()
                        }
                        Button {
                            text: qsTr("ROI")
                            checkable: true
                            enabled: appController.hasDocument && !appController.volumeViewActive
                            checked: imageViewport.roiMode
                            onClicked: imageViewport.roiMode = checked
                        }
                        Button {
                            text: qsTr("Stop")
                            onClicked: appController.stopPlayback()
                        }
                        Item { Layout.fillWidth: true }
                        Label {
                            text: volumeViewport.summary
                            visible: appController.volumeViewActive
                            color: "#aeb7c8"
                        }
                    }
                }

                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    QuickImageViewport {
                        id: imageViewport
                        anchors.fill: parent
                        controller: appController
                        visible: !appController.volumeViewActive
                    }
                    QuickVolumeViewport3D {
                        id: volumeViewport
                        anchors.fill: parent
                        controller: appController
                        visible: appController.volumeViewActive
                        PinchHandler {
                            id: pinchHandler
                            target: null
                            onRotationChanged: function(delta) { volumeViewport.pinchHandlerRotate(centroid.position, delta) }
                            onScaleChanged: function(delta) { volumeViewport.pinchHandlerScale(centroid.position, delta) }
                            onTranslationChanged: function(delta) { volumeViewport.pinchHandlerTranslate(centroid.position, delta) }
                        }
                    }
                }
            }
        }
    }
}
