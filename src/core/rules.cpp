#include "rules.h"

#include <algorithm>
#include <cctype>
#include <charconv>

namespace sf
{

namespace
{

constexpr std::size_t kMaxVoiceDataBytes = 16384;
constexpr std::size_t kMaxMessageBytes = 16384;
constexpr std::uint32_t kMaxPacketsPerMessage = 64;
constexpr std::size_t kBytesPerPacket = 4;
constexpr auto kBudgetWindow = std::chrono::seconds(1);
constexpr std::size_t kMaxMessagesPerWindow = 192;
constexpr std::size_t kMaxCostPerWindow = 256 * 1024;
constexpr std::uint32_t kMaxPacketsPerWindow = 1024;

} // namespace

int HumanPlayers(int steam_accounts, int controllers, bool entity_system_ready)
{
	if (!entity_system_ready)
		return steam_accounts > 0 ? steam_accounts : -1;
	return std::max(std::max(steam_accounts, controllers), 0);
}

std::optional<std::uint32_t> AccountIdFromSteamId64(std::string_view text)
{
	std::uint64_t id = 0;
	const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), id);
	if (error != std::errc() || end != text.data() + text.size())
		return std::nullopt;
	const auto universe = static_cast<std::uint32_t>(id >> 56);
	const auto type = static_cast<std::uint32_t>((id >> 52) & 0xF);
	const auto account = static_cast<std::uint32_t>(id & 0xFFFFFFFF);
	if (universe != 1 || type != 1 || account == 0) // public universe, individual account
		return std::nullopt;
	return account;
}

std::vector<std::uint32_t> ParseAccountList(std::string_view text, std::vector<std::string_view> *rejected)
{
	std::vector<std::uint32_t> accounts;
	std::size_t i = 0;
	while (i < text.size())
	{
		if (text[i] == ',' || std::isspace(static_cast<unsigned char>(text[i])))
		{
			++i;
			continue;
		}
		std::size_t end = i;
		while (end < text.size() && text[end] != ',' && !std::isspace(static_cast<unsigned char>(text[end])))
			++end;
		const std::string_view entry = text.substr(i, end - i);
		if (const auto account = AccountIdFromSteamId64(entry))
			accounts.push_back(*account);
		else if (rejected)
			rejected->push_back(entry);
		i = end;
	}
	return accounts;
}

int RaisedLimit(int current, int max_players)
{
	if (max_players < 1 || max_players > 64)
		return current;
	return std::max(current, max_players);
}

VoiceVerdict CheckVoiceMessage(const VoiceMessage &message)
{
	if (!message.has_audio || !message.has_xuid || message.xuid == 0 || message.xuid != message.sender || !message.format_valid)
		return VoiceVerdict::Malformed;
	// Packets announced with no data to go with them.
	if ((message.offsets != 0 || message.packets != 0) && message.voice_bytes == 0)
		return VoiceVerdict::Malformed;
	if (message.offsets > kMaxPacketsPerMessage || message.packets > kMaxPacketsPerMessage
		|| message.voice_bytes > kMaxVoiceDataBytes || message.message_bytes > kMaxMessageBytes
		|| VoiceCost(message) > kMaxMessageBytes)
		return VoiceVerdict::TooBig;
	return VoiceVerdict::Pass;
}

std::size_t VoiceCost(const VoiceMessage &message)
{
	return message.message_bytes + kBytesPerPacket * std::max(message.offsets, message.packets);
}

bool VoiceBudget::Take(Clock::time_point now, std::size_t cost, std::uint32_t packets)
{
	while (!recent_.empty() && now - recent_.front().time >= kBudgetWindow)
	{
		cost_ -= recent_.front().cost;
		packets_ -= recent_.front().packets;
		recent_.pop_front();
	}
	if (recent_.size() >= kMaxMessagesPerWindow || cost_ + cost > kMaxCostPerWindow || packets_ + packets > kMaxPacketsPerWindow)
		return false;
	recent_.push_back({now, cost, packets});
	cost_ += cost;
	packets_ += packets;
	return true;
}

} // namespace sf
