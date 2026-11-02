#include "youtube_links.hpp"
#include <QCoreApplication>
#include <cassert>

int main(int argc,char** argv){
    QCoreApplication app(argc,argv);
    const QString id="FdlHXjvANfc";
    const auto shortLink=findYouTubeLinks("Музыка https://youtu.be/FdlHXjvANfc?is=abc.");
    assert(shortLink.size()==1&&shortLink[0].id==id);
    assert(findYouTubeLinks("https://www.youtube.com/watch?v=FdlHXjvANfc&t=3").size()==1);
    assert(findYouTubeLinks("https://m.youtube.com/shorts/FdlHXjvANfc").size()==1);
    assert(findYouTubeLinks("https://youtube.com/live/FdlHXjvANfc").size()==1);
    assert(findYouTubeLinks("https://youtube.com.evil.example/watch?v=FdlHXjvANfc").isEmpty());
    assert(findYouTubeLinks("https://youtube.com@evil.example/watch?v=FdlHXjvANfc").isEmpty());
    assert(findYouTubeLinks("https://youtube.com/watch?v=invalid").isEmpty());
    assert(findYouTubeLinks("https://example.com/FdlHXjvANfc").isEmpty());
    const auto html=donationTextWithYouTubeLinks(
        "<script> & https://youtu.be/FdlHXjvANfc?t=1 + https://evil.example");
    assert(html.contains("&lt;script&gt; &amp;"));
    assert(html.contains("href=\"ozkvideo:FdlHXjvANfc\""));
    assert(!html.contains("href=\"https://evil.example"));
    return 0;
}
