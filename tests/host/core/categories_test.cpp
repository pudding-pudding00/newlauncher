// Eigene Kategorien (M17, Spec 10.3): <data>/categories.ini, [<Kategorie>] mit einem Spiel-Schlüssel pro Zeile.
#include "core/categories.hpp"
#include <doctest/doctest.h>
#include <string>
using namespace nl;

TEST_CASE("Categories: Namen in Dateireihenfolge, Schlüssel je Kategorie") {
    const Categories c = Categories::fromText("; meine Listen\n[Party]\nRMCP01\n  nes/smb  \n[Retro]\nnes/smb\n");
    REQUIRE(c.names().size() == 2);
    CHECK(c.names()[0] == "Party");
    CHECK(c.names()[1] == "Retro");
    CHECK(c.contains("Party", "RMCP01"));
    CHECK(c.contains("Party", "nes/smb"));
    CHECK(c.contains("Retro", "nes/smb"));
    CHECK_FALSE(c.contains("Retro", "RMCP01"));
    CHECK_FALSE(c.contains("Fehlt", "RMCP01"));
}

TEST_CASE("Categories: leere Kategorie erscheint, Kommentare zählen nicht als Schlüssel") {
    const Categories c = Categories::fromText("[Leer]\n; RMCP01\n# auch nicht\n\n[Voll]\nRMCP01\n");
    REQUIRE(c.names().size() == 2);
    CHECK(c.names()[0] == "Leer");
    CHECK_FALSE(c.contains("Leer", "RMCP01"));
}

TEST_CASE("Categories: set(false) entfernt den Schlüssel, Kommentare und Reihenfolge bleiben") {
    Categories c = Categories::fromText("; Kopf\n[Party]\n; Lieblinge\nRMCP01\nSMNP01\n[Retro]\nnes/smb\n");
    c.set("Party", "RMCP01", false);
    CHECK_FALSE(c.contains("Party", "RMCP01"));
    CHECK(c.toText() == "; Kopf\n[Party]\n; Lieblinge\nSMNP01\n[Retro]\nnes/smb\n");
}

TEST_CASE("Categories: set(true) hängt an die Kategorie an oder legt sie neu an") {
    Categories c = Categories::fromText("[Party]\nRMCP01\n\n[Retro]\nnes/smb\n");
    c.set("Party", "SMNP01", true);
    c.set("Party", "SMNP01", true);  // schon drin → keine zweite Zeile
    CHECK(c.toText() == "[Party]\nRMCP01\nSMNP01\n\n[Retro]\nnes/smb\n");
    c.set("Neu", "GM8P01", true);
    CHECK(c.contains("Neu", "GM8P01"));
    CHECK(c.names().back() == "Neu");
    CHECK(c.toText() == "[Party]\nRMCP01\nSMNP01\n\n[Retro]\nnes/smb\n\n[Neu]\nGM8P01\n");
}

TEST_CASE("Categories: CRLF-Dateien bekommen CRLF-Zeilen, fehlender Zeilenumbruch am Ende wird ergänzt") {
    Categories c = Categories::fromText("[Party]\r\nRMCP01");
    c.set("Party", "SMNP01", true);
    CHECK(c.toText() == "[Party]\r\nRMCP01\r\nSMNP01\r\n");
}

TEST_CASE("Categories: höchstens 64 Kategorien") {
    std::string text;
    for (int i = 0; i < 70; ++i) text += "[K" + std::to_string(i) + "]\nRMCP01\n";
    Categories c = Categories::fromText(text);
    CHECK(c.names().size() == kMaxCategories);
    CHECK_FALSE(c.contains("K64", "RMCP01"));
    CHECK_FALSE(c.set("Neu", "RMCP01", true));
    CHECK(c.toText() == text);
}

TEST_CASE("Categories: gleichnamige Abschnitte werden zusammengefasst") {
    Categories c = Categories::fromText("[Party]\nRMCP01\n[Retro]\n[Party]\nSMNP01\n");
    CHECK(c.names().size() == 2);
    CHECK(c.contains("Party", "SMNP01"));
    c.set("Party", "RMCP01", false);
    c.set("Party", "SMNP01", false);
    CHECK(c.toText() == "[Party]\n[Retro]\n[Party]\n");
}
