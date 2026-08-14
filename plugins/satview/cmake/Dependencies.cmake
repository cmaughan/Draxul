include(FetchContent)

# Vallado/CelesTrak SGP4 is a SatView implementation detail. Keep the pinned
# archive declaration in the product so removing the submodule removes the
# download and target from the Draxul build graph as well.
FetchContent_Declare(
    celestrak_sgp4
    URL https://celestrak.org/publications/AIAA/2006-6753/AIAA-2006-6753.zip
    URL_HASH SHA256=3642043B706C76BE87CF012DB3F22E04DA6B80498D00F515E51879E0FFADC115
    SOURCE_SUBDIR cmake-no-add-subdirectory)
FetchContent_MakeAvailable(celestrak_sgp4)
