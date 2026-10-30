// Modified 2026-10-30 for Sunshine-Moonlight; contributor: GPT-6 Astra (AI-assisted).
#include "focusclipboard.h"
#include "backend/nvcomputer.h"
#include "backend/identitymanager.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QEventLoop>
#include <QTimer>
#include <QDebug>
#ifdef Q_OS_WIN
#include "focusclipboard_win.h"
#endif

FocusClipboard::FocusClipboard(NvComputer* computer) :
    certificate(computer->serverCert), ssl(IdentityManager::get()->getSslConfig())
{
    base.setScheme("https");base.setHost(computer->activeAddress.address());base.setPort(computer->activeHttpsPort);
}
FocusClipboard::~FocusClipboard(){stopping=true;wait();}
void FocusClipboard::run()
{
#ifdef Q_OS_WIN
    using namespace focusclip;
    try {
        require(!certificate.isNull(),"Clipboard requires a paired host certificate");
        QNetworkAccessManager network;
        QObject::connect(&network,&QNetworkAccessManager::encrypted,&network,[&](QNetworkReply* reply){
            if(reply->sslConfiguration().peerCertificate()!=certificate)reply->abort();
        });
        QObject::connect(&network,&QNetworkAccessManager::sslErrors,&network,[&](QNetworkReply* reply,const QList<QSslError>& errors){
            for(const auto& e:errors)if(e.certificate()!=certificate)return;
            reply->ignoreSslErrors(errors);
        });
        auto request=[&](const char* op,const std::string& data=std::string()) {
            require(!stopping,"Clipboard stopped");QUrl url=base;url.setPath(QStringLiteral("/api/v1/focus-clipboard/")+QString::fromLatin1(op));
            QNetworkRequest req(url);req.setSslConfiguration(ssl);req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::ManualRedirectPolicy);
            req.setHeader(QNetworkRequest::ContentTypeHeader,"application/octet-stream");
            auto reply=network.post(req,QByteArray(data.data(),int(data.size())));QEventLoop loop;QTimer timer;timer.setInterval(50);
            unsigned ticks=0;bool oversized=false;
            QObject::connect(reply,&QNetworkReply::finished,&loop,&QEventLoop::quit);
            QObject::connect(reply,&QNetworkReply::readyRead,&loop,[&]{if(reply->bytesAvailable()>qint64(max_text+max_entries*2020+24)){oversized=true;reply->abort();}});
            QObject::connect(&timer,&QTimer::timeout,&loop,[&]{if(stopping||++ticks>=200)reply->abort();});timer.start();
            loop.exec();timer.stop();int status=reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            bool trusted=reply->sslConfiguration().peerCertificate()==certificate;
            auto bytes=reply->readAll();auto error=reply->error();delete reply;
            require(trusted,"Host certificate changed");require(!oversized,"Clipboard response too large");
            require(status!=404,"Host does not support Focus clipboard");
            require(error==QNetworkReply::NoError && status==200,"Clipboard request failed or offer changed");return std::string(bytes.constData(),size_t(bytes.size()));
        };
        Clipboard clipboard;DWORD local_seq=Clipboard::sequence();uint64_t remote_id=0;bool initialized=false;std::string last_error;
        while(!stopping) {
            try {
                MSG message;while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}
                if(!initialized){auto remote=decode(request("state"));remote_id=remote.id;initialized=true;qInfo()<<"Bidirectional text/file clipboard connected";}
                auto seq=Clipboard::sequence();
                if(seq!=local_seq) {
                    auto offer=clipboard.read();require(seq==Clipboard::sequence(),"Local clipboard changed while reading");
                    if(offer.kind) {
                        request("begin",encode(offer));
                        try {
                            for(size_t i=0;i<offer.files.size();i++)for(uint64_t offset=0;offset<offer.files[i].size;){
                                require(seq==Clipboard::sequence(),"Local copy superseded transfer");auto chunk=read_chunk(offer,i,offset);request("write",chunk_request(offer.id,i,offset)+chunk);offset+=chunk.size();
                            }
                            require(seq==Clipboard::sequence(),"Local copy superseded transfer");std::string id;put(id,offer.id,8);request("commit",id);remote_id=offer.id;
                        } catch(...) {try{std::string id;put(id,offer.id,8);request("cancel",id);}catch(...){}throw;}
                    }
                    local_seq=seq;
                }
                auto remote=decode(request("state"));
                if(remote.id!=remote_id) {
                    auto baseline=Clipboard::sequence();require(baseline==local_seq,"New local copy pending");
                    if(remote.kind) {
                        Incoming incoming(remote,cache_dir(L"Moonlight-Focus"),baseline);
                        for(size_t i=0;i<remote.files.size();i++)for(uint64_t offset=0;offset<remote.files[i].size;){
                            require(baseline==Clipboard::sequence(),"Local copy superseded download");auto data=request("read",chunk_request(remote.id,i,offset));incoming.append(i,offset,data);offset+=data.size();
                        }
                        // Never publish stale files if the remote clipboard changed during download.
                        require(decode(request("state")).id==remote.id,"Remote copy superseded download");incoming.complete();clipboard.write(incoming.offer,baseline);incoming.committed=true;local_seq=Clipboard::sequence();
                    }
                    remote_id=remote.id;
                }
                last_error.clear();
            }catch(const std::exception& e){
                if(stopping)break;
                if(last_error!=e.what()){last_error=e.what();qWarning()<<"Focus clipboard:"<<e.what();}
                if(last_error=="Host does not support Focus clipboard" || last_error=="Host certificate changed")break;
            }
            for(int i=0;i<6&&!stopping;i++)QThread::msleep(50);
        }
    }catch(const std::exception& e){qWarning()<<"Focus clipboard unavailable:"<<e.what();}
#endif
}
