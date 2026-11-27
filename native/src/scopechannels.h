#pragma once
#include <QVariantList>
#include <QVariantMap>
#include <array>
#include <algorithm>
#include <cmath>

struct ScopeChannel {
    bool visible = true;
    double voltsPerDiv = 1.0;
    double offset = 0.0;
    bool current = false;
    QString unit() const { return current ? QStringLiteral("A") : QStringLiteral("V"); }
    double peak() const { return voltsPerDiv * 4.0; }
    double voltageAt(double fraction) const { return offset + (0.5-fraction)*voltsPerDiv*8.0; }
};

class ScopeChannels {
public:
    const ScopeChannel &at(int channel) const { return m_channels.at(size_t(channel-1)); }
    bool setScale(int channel,double voltsPerDiv,double offset) {
        if(channel<1||channel>4||!std::isfinite(voltsPerDiv)||!std::isfinite(offset)||voltsPerDiv<=0)return false;
        auto &c=m_channels[size_t(channel-1)];
        const double scale=std::clamp(voltsPerDiv,c.current?0.000001:0.01,c.current?2.5:100.0);
        const double position=std::clamp(offset,-4.0*scale,4.0*scale);
        if(c.voltsPerDiv==scale&&c.offset==position)return false;
        c.voltsPerDiv=scale;c.offset=position;return true;
    }
    bool setVisible(int channel,bool visible) {
        if(channel<1||channel>4)return false;
        auto &c=m_channels[size_t(channel-1)];
        if(c.visible==visible)return false;
        c.visible=visible;return true;
    }
    bool setCurrent(int channel,bool current) {
        if(channel<1||channel>4)return false;
        auto &c=m_channels[size_t(channel-1)];if(c.current==current)return false;
        c.current=current;c.voltsPerDiv=current?.01:1.0;c.offset=0;return true;
    }
    static int currentRange(double peak,bool high) {
        if(!std::isfinite(peak)||peak<=0||peak>(high?10.0:.3))return -1;
        if(high)return peak<=3?5:6;
        const double limits[]={.0005,.002,.01,.125,.3};
        for(int i=0;i<5;++i)if(peak<=limits[i])return i;
        return -1;
    }
    QVariantList values() const {
        QVariantList result;
        for(const auto &c:m_channels)result.append(QVariantMap{{"visible",c.visible},{"voltsPerDiv",c.voltsPerDiv},{"offset",c.offset},{"current",c.current},{"unit",c.unit()}});
        return result;
    }
private:
    std::array<ScopeChannel,4> m_channels{{{true,1,0},{true,1,0},{false,1,0},{false,1,0}}};
};
