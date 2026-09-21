#include <draxul/weather_service.h>

int main()
{
    draxul::WeatherService weather(nullptr);
    weather.start("");
    return weather.display_text().empty() ? 0 : 1;
}
