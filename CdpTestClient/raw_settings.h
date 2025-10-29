#pragma once

#include <QSettings>
#include <QMap>
#include <QMutex>

/*
   read/write stuff like that -> only reading is needed and tested -- so writing may fail
   [Event_Hit_Breakpoint]
   request=IGNORE
   response={"method":"Debugger.paused","params":{"callFrames":[],"hitBreakpoints":["1"],"reason":"other"}}

   [Debugger.removeBreakpoint]
   request={"id":3,"method":"Debugger.removeBreakpoint","params":{"breakpointId":"1"}}
   response={"id":3,"result":{}}
   )";
 */
class RawSettings : public QSettings {
    public:
        explicit RawSettings(const QString& fileName);
        ~RawSettings();

        bool fileExists() const;

        // Return groups in the order they are defined in the file
        QStringList orderedGroups() const;

        void parseAndDebug();

    private:
        // store instance data as registerFormat() needs static methods
        struct InstanceData {
            RawSettings* instance = nullptr;
            QStringList groupOrder;
        };

        static inline QMap<QString, InstanceData> instanceData_;
        static inline QMutex mutex_;

        // Non-static methods for read and write logic -- here we implement the code to handle the settings file format
        bool readCustomNonStatic(QIODevice& device, QSettings::SettingsMap& map, QStringList& groupOrder);
        bool writeCustomNonStatic(QIODevice& device, const QSettings::SettingsMap& map, const QStringList& groupOrder);

        // Static methods for QSettings::registerFormat -- basically just glue code
        static bool readCustomWrapper(QIODevice& device, QSettings::SettingsMap& map);
        static bool writeCustomWrapper(QIODevice& device, const QSettings::SettingsMap& map);
        static bool getInstanceData(QIODevice& device, RawSettings::InstanceData*& data, QString& fileName);

        QSettings::Format createFormat(const QString &fileName);
};
