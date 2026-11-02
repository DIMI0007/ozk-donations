#pragma once
#include <QMap>
#include <QString>
#include <cmath>

struct RubConversion {double amount=0;bool complete=true,converted=false;};
inline RubConversion convertToRub(const QMap<QString,double>& amounts,const QMap<QString,double>& rates) {
    RubConversion result;
    for(auto it=amounts.cbegin();it!=amounts.cend();++it){
        const double rate=it.key()=="RUB"?1.0:rates.value(it.key(),0);
        if(!std::isfinite(rate)||rate<=0){result.complete=false;continue;}
        if(it.key()!="RUB")result.converted=true;
        result.amount+=it.value()*rate;
    }
    return result;
}
