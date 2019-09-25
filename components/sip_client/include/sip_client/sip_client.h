/*
   Copyright 2017 Christian Taedcke <hacking@taedcke.com>

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
 */

#pragma once

#include "sip_packet.h"

#include "boost/sml.hpp"

#include <cstdlib>
#include <functional>
#include <string>

namespace sml = boost::sml;

// event for sip sml state machine
struct ev_start
{
};
struct ev_401_unauthorized
{
};
struct ev_initiate_call
{
};
struct ev_200_ok
{
};
struct ev_183_session_progress
{
};
struct ev_100_trying
{
};
struct ev_request_call
{
	const std::string local_number;
	const std::string caller_display;
};
struct ev_cancel_call
{
};

struct ev_rx_invite
{
};

struct ev_rx_bye
{
};

struct ev_487_request_cancelled
{
};

struct ev_486_busy_here
{
};

struct ev_603_decline
{
};

struct ev_500_internal_server_error
{
};


struct SipClientEvent
{
    enum class Event
    {
        CALL_START,
        CALL_CANCELLED,
        CALL_END,
        BUTTON_PRESS,
    };

    enum class CancelReason
    {
        UNKNOWN,
        CALL_DECLINED,
        TARGET_BUSY,
    };

    Event event;
    char button_signal = ' ';
    uint16_t button_duration = 0;
    CancelReason cancel_reason = CancelReason::UNKNOWN;
};

template <class SocketT, class Md5T, template<typename> typename SmT>
class SipClientInt
{
public:
SipClientInt(asio::io_context& io_context, const std::string& user, const std::string& pwd, const std::string& server_ip, const std::string& server_port, const std::string& my_ip, sml::sm<SmT<SipClientInt<SocketT, Md5T, SmT>>>& sm)
	    : m_socket(io_context, server_ip, server_port, LOCAL_PORT, [this](std::string data) {

			    rx(data);
		    })
		, m_rtp_socket(io_context, server_ip, "7078", LOCAL_RTP_PORT, [this](std::string data) {
				ESP_LOGV("RTP", "Received %d byte", data.size());
			})
        , m_server_ip(server_ip)
        , m_user(user)
        , m_pwd(pwd)
        , m_my_ip(my_ip)
        , m_uri("sip:" + server_ip)
        , m_to_uri("sip:" + user + "@" + server_ip)
        , m_sip_sequence_number(std::rand() % 2147483647)
        , m_call_id(std::rand() % 2147483647)
        , m_response("")
        , m_realm("")
        , m_nonce("")
        , m_tag(std::rand() % 2147483647)
        , m_branch(std::rand() % 2147483647)
        , m_caller_display(m_user)
        , m_sdp_session_id(0)
        , m_command_event_group(xEventGroupCreate())
	, m_sm(sm)
	, m_io_context(io_context)
    {
    }

    ~SipClientInt()
    {
    }

    bool init()
    {
        bool result_rtp = m_rtp_socket.init();
        bool result_sip = m_socket.init();

	//TODO: remove this here, do it properly with boost::sml
	if ( result_rtp && result_sip)
	{
		m_sm.process_event(ev_start {});
	}


        return result_rtp && result_sip;
    }

    bool is_initialized() const
    {
        return m_socket.is_initialized();
    }

    void set_server_ip(const std::string& server_ip)
    {
        m_server_ip = server_ip;
        m_socket.set_server_ip(server_ip);
        m_rtp_socket.set_server_ip(server_ip);
        m_uri = "sip:" + server_ip;
        m_to_uri = "sip:" + m_user + "@" + server_ip;
    }

    void set_my_ip(const std::string& my_ip)
    {
        m_my_ip = my_ip;
    }

    void set_credentials(const std::string& user, const std::string& password)
    {
        m_user = user;
        m_pwd = password;
        m_to_uri = "sip:" + m_user + "@" + m_server_ip;
    }

    void set_event_handler(std::function<void(const SipClientEvent&)> handler)
    {
        m_event_handler = handler;
    }

    /**
     * Initiate a call async
     *
     * \param[in] local_number A number that is registered locally on the server, e.g. "**610"
     * \param[in] caller_display This string is displayed on the caller's phone
     */
    void request_ring(const std::string& local_number, const std::string& caller_display)
    {
            ESP_LOGI(TAG, "Request to call %s...", local_number.c_str());
	    asio::dispatch(m_io_context, [this, local_number, caller_display](){
			    ESP_LOGI(TAG, "Request to call %s...", local_number.c_str());
			    this->m_sm.process_event(ev_request_call {local_number, caller_display});
		    });
    }

    void request_cancel()
    {
            ESP_LOGI(TAG, "Request to CANCEL call");
	    asio::dispatch(m_io_context, [this](){
			    ESP_LOGI(TAG, "Request to CANCEL call");
			    this->m_sm.process_event(ev_cancel_call {});
		    });
    }

    void deinit()
    {
        m_socket.deinit();
    }

    //send initial register request
    void register_unauth()
    {
	    //sending REGISTER without auth
            m_tag = std::rand() % 2147483647;
            m_branch = std::rand() % 2147483647;
            send_sip_register();
            m_tag = std::rand() % 2147483647;
            m_branch = std::rand() % 2147483647;
    }

    //send  register request
    void register_auth()
    {
	    m_sip_sequence_number++;
	    //sending REGISTER with auth
	    compute_auth_response("REGISTER", "sip:" + m_server_ip);
            send_sip_register();
    }

    void is_registered()
    {
	    m_sip_sequence_number++;
	    m_nonce = "";
	    m_realm = "";
	    m_response = "";
	    ESP_LOGI(TAG, "OK :)");
	    m_uri = "sip:**613@" + m_server_ip;
	    m_to_uri = "sip:**613@" + m_server_ip;
    }

    void send_invite(const ev_401_unauthorized& event)
    {
	    //first ack the prev sip 401/407 packet

	    //expected ack after 401 after we sent invite
	    send_sip_ack();

	    m_sdp_session_id = std::rand();
	    
            // or sending INVITE with auth
            m_branch = std::rand() % 2147483647;
	    m_sip_sequence_number++;
            compute_auth_response("INVITE", m_uri);
            send_sip_invite();

    }

    void send_invite(const ev_initiate_call& event)
    {
	    //sending INVITE without auth
            //m_tag = std::rand() % 2147483647;
	    m_sip_sequence_number++;
            m_sdp_session_id = std::rand();
	    m_branch = std::rand() % 2147483647;
            send_sip_invite();
    }

    void request_call(const ev_request_call& event)
    {
	    ESP_LOGI(TAG, "Request to call %s...", event.local_number.c_str());
            m_call_id = std::rand() % 2147483647;
            m_uri = "sip:" + event.local_number + "@" + m_server_ip;
            m_to_uri = "sip:" + event.local_number + "@" + m_server_ip;
            m_caller_display = event.caller_display;
	    m_sm.process_event(ev_initiate_call {});		
    }

    void cancel_call(const ev_cancel_call& event)
    {
	    ESP_LOGD(TAG, "Sending cancel request");
	    send_sip_cancel();

	    //TODO: if call in progress, send bye
	    //ESP_LOGD(TAG, "Sending bye request");
	    //send_sip_bye();
    }

    void handle_invite(const ev_rx_invite& event)
    {
	    //received an invite, answered it already with ok, so new call is established, because someone called us
	    if (m_event_handler)
	    {
                    m_event_handler(SipClientEvent { SipClientEvent::Event::CALL_START });
	    }

    }

    void call_established()
    {
	    //ack to ok after invite
	    send_sip_ack();
	    if (m_event_handler)
	    {
                    m_event_handler(SipClientEvent { SipClientEvent::Event::CALL_START });
	    }
    }

    void call_cancelled()
    {
	    if (m_event_handler)
	    {
                    m_event_handler(SipClientEvent { SipClientEvent::Event::CALL_CANCELLED });
	    }
	    send_sip_ack();
            m_tag = std::rand() % 2147483647;
            m_branch = std::rand() % 2147483647;
	    m_sip_sequence_number++;
    }

    void call_declined(const ev_486_busy_here&)
    {
	    if (m_event_handler)
	    {
                    m_event_handler(SipClientEvent { SipClientEvent::Event::CALL_CANCELLED, ' ', 0, SipClientEvent::CancelReason::TARGET_BUSY });
	    }
	    
    }

    void call_declined(const ev_603_decline&)
    {
	    if (m_event_handler)
	    {
                    m_event_handler(SipClientEvent { SipClientEvent::Event::CALL_CANCELLED, ' ', 0, SipClientEvent::CancelReason::CALL_DECLINED });
	    }
	    
    }

    void handle_bye()
    {
	    m_sip_sequence_number++;
	    if (m_event_handler)
	    {
                    m_event_handler(SipClientEvent { SipClientEvent::Event::CALL_END });
	    }
    }

    void handle_internal_server_error()
    {
	    m_tag = std::rand() % 2147483647;
            m_branch = std::rand() % 2147483647;
	    m_sip_sequence_number++;

	    // wait for timeout and restart again
	    asio::steady_timer t(m_io_context, asio::chrono::seconds(5));
	    t.async_wait([this](const asio::error_code&)
			 {
				 this->m_sm.process_event(ev_start {});
			 });
    }

    
private:
    
    void rx(std::string recv_string)
    {
#if 0
        if (m_state == SipState::ERROR)
        {
		//TODO: delay is evil for asio
		//vTaskDelay(2000 / portTICK_RATE_MS);
            m_sip_sequence_number++;
            m_state = SipState::IDLE;
            log_state_transition(SipState::ERROR, m_state);
            return;
        }
#endif
        if (recv_string.empty())
        {
            return;
        }

        SipPacket packet(recv_string.c_str(), recv_string.size());
        if (!packet.parse())
        {
            ESP_LOGI(TAG, "Parsing the packet failed");
            return;
        }

        SipPacket::Status reply = packet.get_status();
        ESP_LOGI(TAG, "Parsing the packet ok, reply code=%d", (int)packet.get_status());

        if (reply == SipPacket::Status::SERVER_ERROR_500)
        {
	    m_sm.process_event(ev_500_internal_server_error {});
            return;
        }
        else if ((reply == SipPacket::Status::UNAUTHORIZED_401) || (reply == SipPacket::Status::PROXY_AUTH_REQ_407))
        {
            m_realm = packet.get_realm();
            m_nonce = packet.get_nonce();
        }
        else if ((reply == SipPacket::Status::UNKNOWN) && ((packet.get_method() == SipPacket::Method::NOTIFY) || (packet.get_method() == SipPacket::Method::BYE) || (packet.get_method() == SipPacket::Method::INFO) ))
        {
            send_sip_ok(packet);
        }

        if (!packet.get_contact().empty())
        {
            m_to_contact = packet.get_contact();
        }

        if (!packet.get_to_tag().empty())
        {
            m_to_tag = packet.get_to_tag();
        }

	if (reply == SipPacket::Status::UNAUTHORIZED_401)
	{
		m_sm.process_event(ev_401_unauthorized {});
	}
	else if (reply == SipPacket::Status::OK_200)
	{
		m_sm.process_event(ev_200_ok {});
	}
	else if (reply == SipPacket::Status::TRYING_100)
	{
		m_sm.process_event(ev_100_trying {});
	}
	else if (reply == SipPacket::Status::SESSION_PROGRESS_183)
	{
		m_sm.process_event(ev_183_session_progress {});
	}
	else if (reply == SipPacket::Status::PROXY_AUTH_REQ_407)
	{
		m_sm.process_event(ev_401_unauthorized {});
	}
	else if (reply == SipPacket::Status::REQUEST_CANCELLED_487)
	{
		m_sm.process_event(ev_487_request_cancelled {});
	}
	else if (reply == SipPacket::Status::DECLINE_603)
	{
		send_sip_ack();
                m_sip_sequence_number++;
                m_branch = std::rand() % 2147483647;

		m_sm.process_event(ev_603_decline {});
	}
	else if (reply == SipPacket::Status::BUSY_HERE_486)
	{
		send_sip_ack();
                m_sip_sequence_number++;
                m_branch = std::rand() % 2147483647;

		m_sm.process_event(ev_486_busy_here {});
	}
	
	if (packet.get_method() == SipPacket::Method::BYE)
	{
		m_sm.process_event(ev_rx_bye {});
	}
	else if ((packet.get_method() == SipPacket::Method::INFO)
		 && (packet.get_content_type() == SipPacket::ContentType::APPLICATION_DTMF_RELAY))
	{
                if (m_event_handler)
                {
			m_event_handler(SipClientEvent { SipClientEvent::Event::BUTTON_PRESS, packet.get_dtmf_signal(), packet.get_dtmf_duration() });
                }
	}

	
	//Do not accept calls to e.g. **9 on fritzbox
	//TODO: detect calls from self to **9 and do not accept them
	if ((packet.get_method() == SipPacket::Method::INVITE) /*&& packet.get_p_called_party_id().empty()*/)
	{
		send_sip_ok(packet);
		m_sm.process_event(ev_rx_invite {});
	}

#if 0
        SipState old_state = m_state;
        switch (m_state)
        {
        case SipState::CANCELLED:
            if (reply == SipPacket::Status::OK_200)
            {
                m_sip_sequence_number++;
                m_state = SipState::REGISTERED;
            }
            break;
        case SipState::ERROR:
            m_sip_sequence_number++;
            m_state = SipState::IDLE;
            break;
        }

        if (old_state != m_state)
        {
            log_state_transition(old_state, m_state);
        }
#endif
    }

    void send_sip_register()
    {
        TxBufferT& tx_buffer = m_socket.get_new_tx_buf();
        std::string uri = "sip:" + m_server_ip;

        send_sip_header("REGISTER", uri, "sip:" + m_user + "@" + m_server_ip, tx_buffer);

        tx_buffer << "Contact: \"" << m_user << "\" <sip:" << m_user << "@" << m_my_ip << ":" << LOCAL_PORT << ";transport=" << TRANSPORT_LOWER << ">\r\n";

        if (!m_response.empty())
        {
            tx_buffer << "Authorization: Digest username=\"" << m_user << "\", realm=\"" << m_realm << "\", nonce=\"" << m_nonce << "\", uri=\"" << uri << "\", algorithm=MD5, response=\"" << m_response << "\"\r\n";
        }
        tx_buffer << "Allow: INVITE, ACK, CANCEL, OPTIONS, BYE, REFER, NOTIFY, MESSAGE, SUBSCRIBE, INFO\r\n";
        tx_buffer << "Expires: 3600\r\n";
        tx_buffer << "Content-Length: 0\r\n";
        tx_buffer << "\r\n";

        m_socket.send_buffered_data();
    }

    void send_sip_invite()
    {
        TxBufferT& tx_buffer = m_socket.get_new_tx_buf();

        send_sip_header("INVITE", m_uri, m_to_uri, tx_buffer);

        tx_buffer << "Contact: \"" << m_user << "\" <sip:" << m_user << "@" << m_my_ip << ":" << LOCAL_PORT << ";transport=" << TRANSPORT_LOWER << ">\r\n";

        if (!m_response.empty())
        {
            //tx_buffer << "Proxy-Authorization: Digest username=\"" << m_user << "\", realm=\"" << m_realm << "\", nonce=\"" << m_nonce << "\", uri=\"" << m_uri << "\", response=\"" << m_response << "\"\r\n";
            tx_buffer << "Authorization: Digest username=\"" << m_user << "\", realm=\"" << m_realm << "\", nonce=\"" << m_nonce << "\", uri=\"" << m_uri << "\", response=\"" << m_response << "\"\r\n";
        }
        tx_buffer << "Content-Type: application/sdp\r\n";
        tx_buffer << "Allow: INVITE, ACK, CANCEL, OPTIONS, BYE, REFER, NOTIFY, MESSAGE, SUBSCRIBE, INFO\r\n";
        m_tx_sdp_buffer.clear();
        m_tx_sdp_buffer << "v=0\r\n"
                        << "o=" << m_user << " " << m_sdp_session_id << " " << m_sdp_session_id << " IN IP4 " << m_my_ip << "\r\n"
                        << "s=sip-client/0.0.1\r\n"
                        << "c=IN IP4 " << m_my_ip << "\r\n"
                        << "t=0 0\r\n"
                        << "m=audio " << LOCAL_RTP_PORT << " RTP/AVP 0 8 101\r\n"
                        //<< "a=sendrecv\r\n"
                        << "a=recvonly\r\n"
                        << "a=rtpmap:101 telephone-event/8000\r\n"
                        << "a=fmtp:101 0-15\r\n"
                        << "a=ptime:20\r\n";

        tx_buffer << "Content-Length: " << m_tx_sdp_buffer.size() << "\r\n";
        tx_buffer << "\r\n";
        tx_buffer << m_tx_sdp_buffer.data();

        m_socket.send_buffered_data();
    }

    /**
     * CANCEL a pending INVITE
     *
     * To match the INVITE, the following parameter must not be changed:
     * * CSeq
     * * From tag value
     */
    void send_sip_cancel()
    {
        TxBufferT& tx_buffer = m_socket.get_new_tx_buf();

        send_sip_header("CANCEL", m_uri, m_to_uri, tx_buffer);

        if (!m_response.empty())
        {
            tx_buffer << "Contact: \"" << m_user << "\" <sip:" << m_user << "@" << m_my_ip << ":" << LOCAL_PORT << ";transport=" << TRANSPORT_LOWER << ">\r\n";
            tx_buffer << "Content-Type: application/sdp\r\n";
            tx_buffer << "Authorization: Digest username=\"" << m_user << "\", realm=\"" << m_realm << "\", nonce=\"" << m_nonce << "\", uri=\"" << m_uri << "\", response=\"" << m_response << "\"\r\n";
        }
        tx_buffer << "Content-Length: 0\r\n";
        tx_buffer << "\r\n";

        m_socket.send_buffered_data();
    }

    void send_sip_ack()
    {
        TxBufferT& tx_buffer = m_socket.get_new_tx_buf();
        //if (m_state == SipState::CALL_START)
        {
	    if (!m_to_contact.empty())
	    {
		    send_sip_header("ACK", m_to_contact, m_to_uri, tx_buffer);
	    }
	    else
	    {
		    send_sip_header("ACK", m_uri, m_to_uri, tx_buffer);
	    }
            //std::string m_sdp_session_o;
            //std::string m_sdp_session_s;
            //std::string m_sdp_session_c;
            //m_tx_sdp_buffer.clear();
            //TODO: populate sdp body
            //m_tx_sdp_buffer << "v=0\r\n"
            //	              << m_sdp_session_o << "\r\n"
            //	      << m_sdp_session_s << "\r\n"
            //	      << m_sdp_session_c << "\r\n"
            //	      << "t=0 0\r\n";
            //TODO: copy each m line and select appropriate a line
            //tx_buffer << "Content-Type: application/sdp\r\n";
            //tx_buffer << "Content-Length: " << m_tx_sdp_buffer.size() << "\r\n";
            //tx_buffer << "Allow-Events: telephone-event\r\n";
            tx_buffer << "Content-Length: 0\r\n";
            tx_buffer << "\r\n";
            //tx_buffer << m_tx_sdp_buffer.data();
        }
#if 0
        else
        {
            send_sip_header("ACK", m_uri, m_to_uri, tx_buffer);
            tx_buffer << "Content-Length: 0\r\n";
            tx_buffer << "\r\n";
        }
#endif
        m_socket.send_buffered_data();
    }

    void send_sip_ok(const SipPacket& packet)
    {
        TxBufferT& tx_buffer = m_socket.get_new_tx_buf();

        send_sip_reply_header("200 OK", packet, tx_buffer);
        tx_buffer << "Content-Length: 0\r\n";
        tx_buffer << "\r\n";

        m_socket.send_buffered_data();
    }

    void send_sip_header(const std::string& command, const std::string& uri, const std::string& to_uri, TxBufferT& stream)
    {
        stream << command << " " << uri << " SIP/2.0\r\n";

        stream << "CSeq: " << m_sip_sequence_number << " " << command << "\r\n";
        stream << "Call-ID: " << m_call_id << "@" << m_my_ip << "\r\n";
        stream << "Max-Forwards: 70\r\n";
        stream << "User-Agent: sip-client/0.0.1\r\n";
        if (command == "REGISTER")
        {
            stream << "From: <sip:" << m_user << "@" << m_server_ip << ">;tag=" << m_tag << "\r\n";
        }
        else if (command == "INVITE")
        {
            stream << "From: \"" << m_caller_display << "\" <sip:" << m_user << "@" << m_server_ip << ">;tag=" << m_tag << "\r\n";
        }
        else
        {
            stream << "From: \"" << m_user << "\" <sip:" << m_user << "@" << m_server_ip << ">;tag=" << m_tag << "\r\n";
        }
        stream << "Via: SIP/2.0/" << TRANSPORT_UPPER << " " << m_my_ip << ":" << LOCAL_PORT << ";branch=z9hG4bK-" << m_branch << ";rport\r\n";

        if ((command == "ACK") && !m_to_tag.empty())
        {
            stream << "To: <" << to_uri << ">;tag=" << m_to_tag << "\r\n";
        }
        else
        {
            stream << "To: <" << to_uri << ">\r\n";
        }
    }

    void send_sip_reply_header(const std::string& code, const SipPacket& packet, TxBufferT& stream)
    {
        stream << "SIP/2.0 " << code << "\r\n";

        stream << "To: " << packet.get_to() << "\r\n";
        stream << "From: " << packet.get_from() << "\r\n";
        stream << "Via: " << packet.get_via() << "\r\n";
        stream << "CSeq: " << packet.get_cseq() << "\r\n";
        stream << "Call-ID: " << packet.get_call_id() << "\r\n";
        stream << "Max-Forwards: 70\r\n";
    }

    bool read_param(const std::string& line, const std::string& param_name, std::string& output)
    {
        std::string param(param_name + "=\"");
        size_t pos = line.find(param);
        if (pos == std::string::npos)
        {
            return false;
        }
        pos += param.size();
        size_t pos_end = line.find("\"", pos);
        if (pos_end == std::string::npos)
        {
            return false;
        }
        output = line.substr(pos, pos_end - pos);
        return true;
    }

    void compute_auth_response(const std::string& method, const std::string& uri)
    {
        std::string ha1_text;
        std::string ha2_text;
        unsigned char hash[16];

        m_response = "";
        std::string data = m_user + ":" + m_realm + ":" + m_pwd;

        m_md5.start();
        m_md5.update(data);
        m_md5.finish(hash);
        to_hex(ha1_text, hash, 16);
        ESP_LOGV(TAG, "Calculating md5 for : %s", data.c_str());
        ESP_LOGV(TAG, "Hex ha1 is %s", ha1_text.c_str());

        data = method + ":" + uri;

        m_md5.start();
        m_md5.update(data);
        m_md5.finish(hash);
        to_hex(ha2_text, hash, 16);
        ESP_LOGV(TAG, "Calculating md5 for : %s", data.c_str());
        ESP_LOGV(TAG, "Hex ha2 is %s", ha2_text.c_str());

        data = ha1_text + ":" + m_nonce + ":" + ha2_text;

        m_md5.start();
        m_md5.update(data);
        m_md5.finish(hash);
        to_hex(m_response, hash, 16);
        ESP_LOGV(TAG, "Calculating md5 for : %s", data.c_str());
        ESP_LOGV(TAG, "Hex response is %s", m_response.c_str());
    }

    void to_hex(std::string& dest, const unsigned char* data, int len)
    {
        static const char hexits[17] = "0123456789abcdef";

        dest = "";
        dest.reserve(len * 2 + 1);
        for (int i = 0; i < len; i++)
        {
            dest.push_back(hexits[data[i] >> 4]);
            dest.push_back(hexits[data[i] & 0x0F]);
        }
    }
#if 0
    void log_state_transition(SipState old_state, SipState new_state)
    {
        ESP_LOGI(TAG, "New state %d -> %d", (int)old_state, (int)new_state);
    }
#endif
    
    SocketT m_socket;
    SocketT m_rtp_socket;
    Md5T m_md5;
    std::string m_server_ip;

    std::string m_user;
    std::string m_pwd;
    std::string m_my_ip;

    std::string m_uri;
    std::string m_to_uri;
    std::string m_to_contact;
    std::string m_to_tag;

    uint32_t m_sip_sequence_number;
    uint32_t m_call_id;

    //auth stuff
    std::string m_response;
    std::string m_realm;
    std::string m_nonce;

    uint32_t m_tag;
    uint32_t m_branch;

    //misc stuff
    std::string m_caller_display;

    uint32_t m_sdp_session_id;
    Buffer<1024> m_tx_sdp_buffer;

    std::function<void(const SipClientEvent&)> m_event_handler;

    /* FreeRTOS event group to signal commands from other tasks */
    EventGroupHandle_t m_command_event_group;

    sml::sm<SmT<SipClientInt<SocketT, Md5T, SmT>>>& m_sm;

    asio::io_context& m_io_context;
    
    static constexpr const uint16_t LOCAL_PORT = 5060;
    static constexpr const char* TRANSPORT_LOWER = "udp";
    static constexpr const char* TRANSPORT_UPPER = "UDP";

    static constexpr uint32_t SOCKET_RX_TIMEOUT_MSEC = 200;
    static constexpr uint16_t LOCAL_RTP_PORT = 7078;
    static constexpr const char* TAG = "SipClient";
};

template <class SipClientT>
struct sip_states
{
    auto operator()() const noexcept
    {
        using namespace sml;

        const auto idle = state<class idle>;

	const auto action_register_unauth = [](SipClientT& sip, const auto& event) {
            (void)event;
            sip.register_unauth();
        };

	const auto action_register_auth = [](SipClientT& sip, const auto& event) {
            (void)event;
            sip.register_auth();
        };

	const auto action_is_registered = [](SipClientT& sip, const auto& event) {
            (void)event;
            sip.is_registered();
        };

	const auto action_send_invite = [](SipClientT& sip, const auto& event) {
            sip.send_invite(event);
        };

	const auto action_request_call = [](SipClientT& sip, const auto& event) {
            sip.request_call(event);
        };

	const auto action_cancel_call = [](SipClientT& sip, const auto& event) {
            sip.cancel_call(event);
        };

	const auto action_rx_invite = [](SipClientT& sip, const auto& event) {
		sip.handle_invite(event);
        };

	const auto action_call_established = [](SipClientT& sip, const auto& event) {
		sip.call_established();
        };

	const auto action_call_cancelled = [](SipClientT& sip, const auto& event) {
		sip.call_cancelled();
        };

	const auto action_call_declined = [](SipClientT& sip, const auto& event) {
		sip.call_declined(event);
        };

	const auto action_rx_bye = [](SipClientT& sip, const auto& event) {
		sip.handle_bye();
        };

	const auto action_rx_internal_server_error = [](SipClientT& sip, const auto& event) {
		sip.handle_internal_server_error();
        };

        return make_transition_table(
            *idle + event<ev_start> / action_register_unauth = "waiting_for_auth_reply"_s,
	    "waiting_for_auth_reply"_s + sml::on_entry<_> / [] { ESP_LOGV("SIP SM", "s1 on entry"); }, "waiting_for_auth_reply"_s + sml::on_exit<_> / [] { ESP_LOGV("SIP SM", "waiting_for_auth_reply on exit"); },
	    "waiting_for_auth_reply"_s + event<ev_401_unauthorized> / action_register_auth = "waiting_for_auth_reply"_s,
	    "waiting_for_auth_reply"_s + event<ev_200_ok> / action_is_registered = "registered"_s,
	    "waiting_for_auth_reply"_s + event<ev_500_internal_server_error> / action_rx_internal_server_error = "idle"_s,
	    "registered"_s + event<ev_request_call> / action_request_call = "registered"_s,
	    "registered"_s + event<ev_initiate_call> / action_send_invite = "calling"_s,
	    "registered"_s + event<ev_rx_invite> / action_rx_invite = "call_established"_s,
	    "calling"_s + event<ev_401_unauthorized> / action_send_invite = "calling"_s,
	    "calling"_s + event<ev_cancel_call> / action_cancel_call = "calling"_s,
	    "calling"_s + event<ev_183_session_progress> = "calling"_s,
	    "calling"_s + event<ev_100_trying> = "calling"_s,
	    "calling"_s + event<ev_200_ok> / action_call_established = "call_established"_s,
	    "calling"_s + event<ev_487_request_cancelled> / action_call_cancelled = "registered"_s,
	    "calling"_s + event<ev_486_busy_here> / action_call_declined = "registered"_s,
	    "calling"_s + event<ev_603_decline> / action_call_declined = "registered"_s,
	    "call_established"_s + event<ev_rx_bye> / action_rx_bye = "registered"_s,
	    "call_established"_s + event<ev_200_ok> = X,
	    "calling"_s + event<ev_200_ok> = X );
    }
};

template <class SocketT, class Md5T>
class SipClient
{
public:
    SipClient(asio::io_context& io_context, const std::string& user, const std::string& pwd, const std::string& server_ip, const std::string& server_port, const std::string& my_ip)
        : m_sip
    {
	    io_context, user, pwd, server_ip, server_port, my_ip, m_sm
    }
    , m_sm
    {
        m_sip
    }
    {
    }

    bool init()
    {
        return m_sip.init();
    }

    bool is_initialized() const
    {
        return m_sip.is_initialized();
    }

    void set_server_ip(const std::string& server_ip)
    {
        m_sip.set_server_ip(server_ip);
    }

    void set_my_ip(const std::string& my_ip)
    {
        m_sip.set_my_ip(my_ip);
    }

    void set_credentials(const std::string& user, const std::string& password)
    {
        m_sip.set_credentials(user, password);
    }

    void set_event_handler(std::function<void(const SipClientEvent&)> handler)
    {
        m_sip.set_event_handler(handler);
    }

    /**
     * Initiate a call async
     *
     * \param[in] local_number A number that is registered locally on the server, e.g. "**610"
     * \param[in] caller_display This string is displayed on the caller's phone
     */
    void request_ring(const std::string& local_number, const std::string& caller_display)
    {
        m_sip.request_ring(local_number, caller_display);
    }

    void request_cancel()
    {
        m_sip.request_cancel();
    }

#if 0
    void run()
    {
        m_sm.process_event(ev_start {});
        //assert(sm.is(sml::X));

        m_sip.run();
    }
#endif
    
    void deinit()
    {
        m_sip.deinit();
    }

    void register_unauth()
    {
	    m_sip.register_unauth();
    }

    void is_registered()
    {
	    m_sip.is_registered();
    }

    void send_invite(const auto& event)
    {
	    m_sip.send_invite(event);
    }

    void request_call(const ev_request_call& event)
    {
	    m_sip.request_call(event);
    }

    void cancel_call(const ev_cancel_call& event)
    {
	    m_sip.cancel_call(event);
    }

    void handle_invite(const ev_rx_invite& event)
    {
	    m_sip.handle_invite(event);
    }

    void call_established()
    {
	    m_sip.call_established();
    }

    void call_cancelled()
    {
	    m_sip.call_cancelled();
    }

    void call_declined(const auto& event)
    {
	    m_sip.call_declined(event);
    }

    void handle_bye()
    {
	    m_sip.handle_bye();
    }

    void handle_internal_server_error()
    {
	    m_sip.handle_internal_server_error();
    }

private:

    using SipClientInternal = SipClientInt<SocketT, Md5T, sip_states>;
    SipClientInternal m_sip;

    sml::sm<sip_states<SipClientInternal>> m_sm;
};
