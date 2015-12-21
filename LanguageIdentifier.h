/// @brief Language detection utility routines.
///
/// Contains the main utility function, guessLanguage(), and all
/// the support routines for detecting the language of a web page.
///

// using a different macro because there's already a Language.h
#ifndef LANGUAGEIDENTIFIER_H
#define LANGUAGEIDENTIFIER_H

#include <stdint.h>

/// Contains methods of language identification by various means.
class LanguageIdentifier {
public:
	static uint8_t guessCountryTLD(const char *url);
};

#endif // LANGUAGEIDENTIFIER_H
