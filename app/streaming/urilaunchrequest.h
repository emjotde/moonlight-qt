#pragma once

#include "streamlaunchrequest.h"

struct UriLaunchParseResult
{
    StreamLaunchRequest request;
    QString error;

    bool isValid() const { return error.isEmpty(); }
};

class UriLaunchRequestParser
{
public:
    static UriLaunchParseResult parse(const QString& uri,
                                      const StreamingPreferences& globalPreferences);

private:
    static bool decodeComponent(const QString& encoded, QString& decoded, QString& error);
    static bool parseInteger(const QString& name, const QString& value,
                             int minimum, int maximum, int& result, QString& error);
};
