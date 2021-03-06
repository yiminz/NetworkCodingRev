#include "rx.h"
#include "encoding_decoding_macro.h"
#include <cstdlib>

#define STRICTLY_ASCENDING_ORDER(a,b,c) ((u16)((u16)(c) - (u16)(a)) > (u16)((u16)(c) - (u16)(b)))

using namespace NetworkCoding;

void PRINT(Header::Data* data)
{
    printf("[Type %hhu][TotalSize %hu][MinSeq. %hu][CurSeq. %hu][MaxSeq. %hu][Exp.Rank %hhu][Max.Rank %hhu][Flags %hhx][TxCnt %hhu][Payload %hu][LastInd. %hhu]",
           data->m_Type,
           ntohs(data->m_TotalSize),
           ntohs(data->m_MinBlockSequenceNumber),
           ntohs(data->m_CurrentBlockSequenceNumber),
           ntohs(data->m_MaxBlockSequenceNumber),
           data->m_ExpectedRank,
           data->m_MaximumRank,
           data->m_Flags,
           data->m_TxCount,
           ntohs(data->m_PayloadSize),
           data->m_LastIndicator);
    printf("[Code ");
    for(u08 i = 0 ; i < data->m_MaximumRank ; i++)
    {
        printf(" %3hhu ", data->m_Codes[i]);
    }
    printf("]\n");
    for(unsigned long i = 0 ; i < ntohs(data->m_PayloadSize) ; i++)
    {
        std::cout<<(data->m_Codes+(data->m_MaximumRank-1))[i];
    }
    std::cout<<std::endl;
}

const u08 ReceptionBlock::FindMaximumRank(Header::Data* hdr)
{
    u08 MaximumRank = 0;
    if(m_EncodedPacketBuffer.size())
    {
        return reinterpret_cast<Header::Data*>(m_EncodedPacketBuffer[0].get())->m_ExpectedRank;
    }
    if(hdr && hdr->m_Flags & Header::Data::DataHeaderFlag::FLAGS_END_OF_BLK)
    {
        return hdr->m_ExpectedRank;
    }
    for(u08 i = 0 ; i < m_DecodedPacketBuffer.size() ; i++)
    {
        if(reinterpret_cast<Header::Data*>(m_DecodedPacketBuffer[i].get())->m_Flags & Header::Data::DataHeaderFlag::FLAGS_END_OF_BLK)
        {
            return reinterpret_cast<Header::Data*>(m_DecodedPacketBuffer[i].get())->m_ExpectedRank;
        }
        else if(MaximumRank < reinterpret_cast<Header::Data*>(m_DecodedPacketBuffer[i].get())->m_ExpectedRank)
        {
            MaximumRank = reinterpret_cast<Header::Data*>(m_DecodedPacketBuffer[i].get())->m_ExpectedRank;
        }
    }
    if(hdr && MaximumRank < hdr->m_ExpectedRank)
    {
        MaximumRank = hdr->m_ExpectedRank;
    }
    return MaximumRank;
}

const bool ReceptionBlock::FindEndOfBlock(Header::Data* hdr)
{
    if(m_EncodedPacketBuffer.size())
    {
        return true;
    }
    if(hdr && hdr->m_Flags & Header::Data::DataHeaderFlag::FLAGS_END_OF_BLK)
    {
        return true;
    }
    for(u08 i = 0 ; i < m_DecodedPacketBuffer.size() ; i++)
    {
        if(reinterpret_cast<Header::Data*>(m_DecodedPacketBuffer[i].get())->m_Flags & Header::Data::DataHeaderFlag::FLAGS_END_OF_BLK)
        {
            return true;
        }
    }
    return false;
}

ReceptionBlock::ReceiveAction ReceptionBlock::FindAction(u08* buffer, u16 length)
{
    const u08 OLD_RANK = m_DecodedPacketBuffer.size() + m_EncodedPacketBuffer.size();
    const u08 MAX_RANK = FindMaximumRank(reinterpret_cast<Header::Data*>(buffer));
    const bool MAKE_DECODING_MATRIX = (OLD_RANK+1 == MAX_RANK && FindEndOfBlock(reinterpret_cast<Header::Data*>(buffer)));

    std::vector< std::unique_ptr< u08[] > > EncodingMatrix;
    if(OLD_RANK == MAX_RANK)
    {
        return DECODING;
    }
    if(MAKE_DECODING_MATRIX)
    {
        for(u08 row = 0 ; row < MAX_RANK ; row++)
        {
            try
            {
                TEST_EXCEPTION(std::bad_alloc());
                m_DecodingMatrix.emplace_back(std::unique_ptr<u08[]>(new u08[MAX_RANK]));
            }
            catch(const std::bad_alloc& ex)
            {
                EXCEPTION_PRINT;
                m_DecodingMatrix.clear();
                return DROP;
            }
            memset(m_DecodingMatrix.back().get(), 0x0, MAX_RANK);
            m_DecodingMatrix.back().get()[row] = 0x01;
        }
    }
    // 1. Allcate Encoding Matrix
    for(u08 row = 0 ; row < MAX_RANK ; row++)
    {
        try
        {
            TEST_EXCEPTION(std::bad_alloc());
            EncodingMatrix.emplace_back(std::unique_ptr<u08[]>(new u08[MAX_RANK]));
            memset(EncodingMatrix.back().get(), 0x0, MAX_RANK);
        }
        catch(const std::bad_alloc& ex)
        {
            EXCEPTION_PRINT;
            m_DecodingMatrix.clear();
            return DROP;
        }
    }
    // 2. Fill-in Encoding Matrix
    {
        u08 DecodedPktIdx = 0;
        u08 EncodedPktIdx = 0;
        u08 RxPkt = 0;
        for(u08 row = 0 ; row < MAX_RANK ; row++)
        {
            if(DecodedPktIdx < m_DecodedPacketBuffer.size() &&
                    reinterpret_cast<Header::Data*>(m_DecodedPacketBuffer[DecodedPktIdx].get())->m_Codes[row])
            {
                memcpy(EncodingMatrix[row].get(),reinterpret_cast<Header::Data*>(m_DecodedPacketBuffer[DecodedPktIdx++].get())->m_Codes, MAX_RANK);
            }
            else if(EncodedPktIdx < m_EncodedPacketBuffer.size() && reinterpret_cast<Header::Data*>(m_EncodedPacketBuffer[EncodedPktIdx].get())->m_Codes[row])
            {
                memcpy(EncodingMatrix[row].get(),reinterpret_cast<Header::Data*>(m_EncodedPacketBuffer[EncodedPktIdx++].get())->m_Codes, MAX_RANK);
            }
            else if(RxPkt < 1 && reinterpret_cast<Header::Data*>(buffer)->m_Codes[row])
            {
                memcpy(EncodingMatrix[row].get(),reinterpret_cast<Header::Data*>(buffer)->m_Codes, MAX_RANK);
                RxPkt++;
            }
        }
    }
    // 3. Elimination
    for(u08 row = 0 ; row < MAX_RANK ; row++)
    {
        if(EncodingMatrix[row].get()[row] == 0)
        {
            continue;
        }
        const u08 MUL = FiniteField::instance()->inv(EncodingMatrix[row].get()[row]);
        for(u08 col = 0 ; col < MAX_RANK ; col++)
        {
            EncodingMatrix[row].get()[col] = FiniteField::instance()->mul(EncodingMatrix[row].get()[col], MUL);
            if(MAKE_DECODING_MATRIX)
            {
                m_DecodingMatrix[row].get()[col] = FiniteField::instance()->mul(m_DecodingMatrix[row].get()[col], MUL);
            }
        }

        for(u08 elimination_row = row+1 ; elimination_row < MAX_RANK ; elimination_row++)
        {
            if(EncodingMatrix[elimination_row].get()[row] == 0)
            {
                continue;
            }
            const u08 MUL2 = FiniteField::instance()->inv(EncodingMatrix[elimination_row].get()[row]);
            for(u08 j = 0 ; j < MAX_RANK ; j++)
            {
                EncodingMatrix[elimination_row].get()[j] = FiniteField::instance()->mul(EncodingMatrix[elimination_row].get()[j], MUL2);
                EncodingMatrix[elimination_row].get()[j] ^= EncodingMatrix[row].get()[j];
                if(MAKE_DECODING_MATRIX)
                {
                    m_DecodingMatrix[elimination_row].get()[j] = FiniteField::instance()->mul(m_DecodingMatrix[elimination_row].get()[j], MUL2);
                    m_DecodingMatrix[elimination_row].get()[j] ^= m_DecodingMatrix[row].get()[j];
                }
            }
        }
    }
    u08 RANK = 0;
    for(u08 i = 0 ; i < MAX_RANK ; i++)
    {
        if(EncodingMatrix[i].get()[i] == 1)
        {
            RANK++;
        }
    }
    if(MAKE_DECODING_MATRIX)
    {
        for(s16 col = MAX_RANK - 1 ; col > -1  ; col--)
        {
            for(s16 row = 0 ; row < col ; row++)
            {
                if(EncodingMatrix[row].get()[col] == 0)
                {
                    continue;
                }
                const u08 MUL = EncodingMatrix[row].get()[col];
                for(u08 j = 0 ; j < MAX_RANK ; j++)
                {
                    EncodingMatrix[row].get()[j] ^= FiniteField::instance()->mul(EncodingMatrix[col].get()[j], MUL);
                    if(MAKE_DECODING_MATRIX)
                    {
                        m_DecodingMatrix[row].get()[j] ^= FiniteField::instance()->mul(m_DecodingMatrix[col].get()[j], MUL);
                    }
                }
            }
        }
    }
    if(RANK == (OLD_RANK+1))
    {
        return ENQUEUE_AND_DECODING;
    }
    if(MAKE_DECODING_MATRIX && m_DecodingMatrix.size())
    {
        m_DecodingMatrix.clear();
    }
    return DROP;
}

bool ReceptionBlock::Decoding()
{
    std::vector< std::unique_ptr< u08[] > > DecodeOut;
    const u08 MAX_RANK = FindMaximumRank();
    u08 EncodedPktIdx = 0;
    if(MAX_RANK != m_DecodedPacketBuffer.size() + m_EncodedPacketBuffer.size())
    {
        return false;
    }
    for(u08 row = 0 ; row < MAX_RANK ; row++)
    {
        if(row < m_DecodedPacketBuffer.size() &&
                reinterpret_cast<Header::Data*>(m_DecodedPacketBuffer[row].get())->m_Codes[row] > 0)
        {
            continue;
        }
        do
        {
            try
            {
                TEST_EXCEPTION(std::bad_alloc());
                m_DecodedPacketBuffer.emplace(m_DecodedPacketBuffer.begin()+row, std::unique_ptr< u08[] >(m_EncodedPacketBuffer[EncodedPktIdx++].release()));
            }
            catch(const std::bad_alloc& ex)
            {
                EXCEPTION_PRINT;
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }
            break;
        }while(1);
    }
    m_EncodedPacketBuffer.clear();
    for(u08 row = 0 ; row < MAX_RANK ; row++)
    {
        Header::Data* const pkt = reinterpret_cast<Header::Data*>(m_DecodedPacketBuffer[row].get());
        if(pkt->m_Flags & Header::Data::DataHeaderFlag::FLAGS_ORIGINAL)
        {
            do
            {
                try
                {
                    TEST_EXCEPTION(std::bad_alloc());
                    DecodeOut.emplace_back(std::unique_ptr< u08[] >(nullptr));
                }
                catch(const std::bad_alloc& ex)
                {
                    EXCEPTION_PRINT;
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    continue;
                }
                break;
            }while(1);
            continue;
        }
        do
        {
            try
            {
                TEST_EXCEPTION(std::bad_alloc());
                DecodeOut.emplace_back(std::unique_ptr< u08[] >(new u08[ntohs(pkt->m_TotalSize)]));
            }
            catch(std::bad_alloc& ex)
            {
                EXCEPTION_PRINT;
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }
            break;
        }while(1);

        memset(DecodeOut.back().get(), 0x0, ntohs(pkt->m_TotalSize));
        for(u32 decodingposition = 0 ; decodingposition < Header::Data::CodingOffset ; decodingposition++)
        {
            DecodeOut.back().get()[decodingposition] = m_DecodedPacketBuffer[row].get()[decodingposition];
        }
        for(u08 i = 0 ; i < MAX_RANK ; i++)
        {
            const u16 length = (ntohs(reinterpret_cast<Header::Data*>(m_DecodedPacketBuffer[i].get())->m_TotalSize) < ntohs(pkt->m_TotalSize)?
                                    ntohs(reinterpret_cast<Header::Data*>(m_DecodedPacketBuffer[i].get())->m_TotalSize):
                                    ntohs(pkt->m_TotalSize));
#if 0
            for(u32 decodingposition = Header::Data::CodingOffset ; decodingposition < length ; decodingposition++)
            {
                DecodeOut.back().get()[decodingposition] ^= FiniteField::instance()->mul(m_DecodingMatrix[row].get()[i], m_DecodedPacketBuffer[i].get()[decodingposition]);
            }
#else
            u32 decodingposition = Header::Data::CodingOffset;
            while(decodingposition < length)
            {
                if(length - decodingposition > 1024)
                {
                    Decoding1024(DecodeOut.back().get(), m_DecodedPacketBuffer, m_DecodingMatrix, decodingposition, i, row);
                }
                else if(length - decodingposition > 512)
                {
                    Decoding512(DecodeOut.back().get(), m_DecodedPacketBuffer, m_DecodingMatrix, decodingposition, i, row);
                }
                else if(length - decodingposition > 256)
                {
                    Decoding256(DecodeOut.back().get(), m_DecodedPacketBuffer, m_DecodingMatrix, decodingposition, i, row);
                }
                else if(length - decodingposition > 128)
                {
                    Decoding128(DecodeOut.back().get(), m_DecodedPacketBuffer, m_DecodingMatrix, decodingposition, i, row);
                }
                else if(length - decodingposition > 64)
                {
                    Decoding64(DecodeOut.back().get(), m_DecodedPacketBuffer, m_DecodingMatrix, decodingposition, i, row);
                }
                else if(length - decodingposition > 32)
                {
                    Decoding32(DecodeOut.back().get(), m_DecodedPacketBuffer, m_DecodingMatrix, decodingposition, i, row);
                }
                else if(length - decodingposition > 16)
                {
                    Decoding16(DecodeOut.back().get(), m_DecodedPacketBuffer, m_DecodingMatrix, decodingposition, i, row);
                }
                else if(length - decodingposition > 8)
                {
                    Decoding8(DecodeOut.back().get(), m_DecodedPacketBuffer, m_DecodingMatrix, decodingposition, i, row);
                }
                else if(length - decodingposition > 4)
                {
                    Decoding4(DecodeOut.back().get(), m_DecodedPacketBuffer, m_DecodingMatrix, decodingposition, i, row);
                }
                else if(length - decodingposition > 2)
                {
                    Decoding2(DecodeOut.back().get(), m_DecodedPacketBuffer, m_DecodingMatrix, decodingposition, i, row);
                }
                else
                {
                    DecodeOut.back().get()[decodingposition] ^= FiniteField::instance()->mul(m_DecodingMatrix[row].get()[i], m_DecodedPacketBuffer[i].get()[decodingposition]);
                    decodingposition++;
                }
            }
#endif
        }
        if(reinterpret_cast<Header::Data*>(DecodeOut.back().get())->m_Codes[row] != 1)
        {
            std::cout<<"Decoding Error\n";
            exit(-1);
        }
    }
    for(u08 i = 0 ; i < DecodeOut.size() ; i++)
    {
        if(DecodeOut[i].get() == nullptr)
        {
            DecodeOut[i].reset(m_DecodedPacketBuffer[i].release());
        }
    }
    for(u08 i = 0 ; i < DecodeOut.size() ; i++)
    {
        u08* pkt = DecodeOut[i].release();
        if(!(reinterpret_cast<Header::Data*>(pkt)->m_Flags & Header::Data::DataHeaderFlag::FLAGS_CONSUMED))
        {
            while(c_Session->m_RxTaskQueue.Enqueue([this, pkt](){
                if(c_Reception->m_RxCallback)
                {
                    c_Reception->m_RxCallback(pkt+sizeof(Header::Data)+reinterpret_cast<Header::Data*>(pkt)->m_MaximumRank-1, ntohs(reinterpret_cast<Header::Data*>(pkt)->m_PayloadSize), &c_Session->m_SenderAddress, sizeof(c_Session->m_SenderAddress));
                }
                delete [] pkt;
            })==false);
        }
        else
        {
            delete [] pkt;
        }
    }
    return true;
}

ReceptionBlock::ReceptionBlock(Reception * const reception, ReceptionSession * const session, const u16 BlockSequenceNumber):c_Reception(reception), c_Session(session), m_BlockSequenceNumber(BlockSequenceNumber)
{
    m_DecodedPacketBuffer.clear();
    m_EncodedPacketBuffer.clear();
    m_DecodingReady = false;
}

ReceptionBlock::~ReceptionBlock()
{
    m_DecodedPacketBuffer.clear();
    m_EncodedPacketBuffer.clear();
}

void ReceptionBlock::Receive(u08 *buffer, u16 length, const sockaddr_in * const sender_addr, const u32 sender_addr_len)
{
    Header::Data* const DataHeader = reinterpret_cast <Header::Data*>(buffer);
    if(m_DecodingReady)
    {
        Header::DataAck ack;
        ack.m_Type = Header::Common::HeaderType::DATA_ACK;
        ack.m_Sequence = DataHeader->m_CurrentBlockSequenceNumber;
        ack.m_Losses = DataHeader->m_TxCount - DataHeader->m_ExpectedRank;
        sendto(c_Reception->c_Socket, (u08*)&ack, sizeof(ack), 0, (sockaddr*)sender_addr, sender_addr_len);
        return;
    }
    switch(FindAction(buffer, length))
    {
    case DROP:
        return;
        break;
    case ENQUEUE_AND_DECODING:
        if(DataHeader->m_Flags & Header::Data::DataHeaderFlag::FLAGS_ORIGINAL)
        {
            try
            {
                TEST_EXCEPTION(std::bad_alloc());
                m_DecodedPacketBuffer.emplace_back(std::unique_ptr<u08[]>(new u08[length]));
            }
            catch(const std::bad_alloc& ex)
            {
                EXCEPTION_PRINT;
                m_DecodingMatrix.clear();
                return;
            }
            memcpy(m_DecodedPacketBuffer.back().get(), buffer, length);
            if(c_Session->m_SequenceNumberForService == ntohs(DataHeader->m_CurrentBlockSequenceNumber)&&
                    DataHeader->m_ExpectedRank == m_DecodedPacketBuffer.size())
            {
                do
                {
                    u08* pkt;
                    try
                    {
                        TEST_EXCEPTION(std::bad_alloc());
                        pkt = new u08[length];
                        memcpy(pkt, buffer, length);
                        while(c_Session->m_RxTaskQueue.Enqueue([this, pkt](){
                            if(c_Reception->m_RxCallback)
                            {
                                c_Reception->m_RxCallback(pkt+sizeof(Header::Data)+reinterpret_cast<Header::Data*>(pkt)->m_MaximumRank-1, ntohs(reinterpret_cast<Header::Data*>(pkt)->m_PayloadSize), &c_Session->m_SenderAddress, sizeof(c_Session->m_SenderAddress));
                            }
                            delete [] pkt;
                        })==false);
                        reinterpret_cast<Header::Data*>(m_DecodedPacketBuffer.back().get())->m_Flags |= Header::Data::DataHeaderFlag::FLAGS_CONSUMED;
                        if(reinterpret_cast<Header::Data*>(m_DecodedPacketBuffer.back().get())->m_Flags & Header::Data::DataHeaderFlag::FLAGS_END_OF_BLK)
                        {
                            c_Session->m_SequenceNumberForService++;
                        }
                        break;
                    }
                    catch(const std::bad_alloc& ex)
                    {
                        EXCEPTION_PRINT;
                    }
                }
                while (1);
            }
        }
        else
        {
            try
            {
                TEST_EXCEPTION(std::bad_alloc());
                m_EncodedPacketBuffer.emplace_back(std::unique_ptr<u08[]>(new u08[length]));
            }
            catch(const std::bad_alloc& ex)
            {
                EXCEPTION_PRINT;
                m_DecodingMatrix.clear();
                return;
            }
            memcpy(m_EncodedPacketBuffer.back().get(), buffer, length);
        }
        // Continue with decoding.
    case DECODING:
        if((DataHeader->m_ExpectedRank == (m_DecodedPacketBuffer.size() + m_EncodedPacketBuffer.size())) &&
                (DataHeader->m_Flags & Header::Data::DataHeaderFlag::FLAGS_END_OF_BLK))
        {
            // Decoding.
            m_DecodingReady = true;
            if(c_Session->m_SequenceNumberForService == ntohs(DataHeader->m_CurrentBlockSequenceNumber))
            {
                ReceptionBlock** pp_block;
                ReceptionBlock* p_block;
                while(c_Session->m_SequenceNumberForService != c_Session->m_MaxSequenceNumberAwaitingAck&&
                      (pp_block = c_Session->m_Blocks.GetPtr(c_Session->m_SequenceNumberForService))
                      )
                {
                    if((*pp_block)->m_DecodingReady)
                    {
                        p_block = (*pp_block);
                        if(p_block->Decoding())
                        {
                            c_Session->m_SequenceNumberForService++;
                        }
                    }
                    else
                    {
                        for(u08 i = 0 ; i < (*pp_block)->m_DecodedPacketBuffer.size() ; i++)
                        {
                            if(i != reinterpret_cast<Header::Data*>((*pp_block)->m_DecodedPacketBuffer[i].get())->m_ExpectedRank-1)
                            {
                                break;
                            }
                            do
                            {
                                u08* pkt;
                                try
                                {
                                    TEST_EXCEPTION(std::bad_alloc());
                                    pkt = new u08[ntohs(reinterpret_cast<Header::Data*>((*pp_block)->m_DecodedPacketBuffer[i].get())->m_TotalSize)];
                                    memcpy(pkt, (*pp_block)->m_DecodedPacketBuffer[i].get(), ntohs(reinterpret_cast<Header::Data*>((*pp_block)->m_DecodedPacketBuffer[i].get())->m_TotalSize));
                                    while(c_Session->m_RxTaskQueue.Enqueue([this, pkt](){
                                        if(c_Reception->m_RxCallback)
                                        {
                                            c_Reception->m_RxCallback(pkt+sizeof(Header::Data)+reinterpret_cast<Header::Data*>(pkt)->m_MaximumRank-1, ntohs(reinterpret_cast<Header::Data*>(pkt)->m_PayloadSize), &c_Session->m_SenderAddress, sizeof(c_Session->m_SenderAddress));
                                        }
                                        delete [] pkt;
                                    })==false);
                                    reinterpret_cast<Header::Data*>((*pp_block)->m_DecodedPacketBuffer[i].get())->m_Flags |= Header::Data::DataHeaderFlag::FLAGS_CONSUMED;
                                    break;
                                }
                                catch (const std::bad_alloc& ex)
                                {
                                    EXCEPTION_PRINT;
                                }
                            }
                            while(1);
                        }
                        break;
                    }
                }
            }
            Header::DataAck ack;
            ack.m_Type = Header::Common::HeaderType::DATA_ACK;
            ack.m_Sequence = DataHeader->m_CurrentBlockSequenceNumber;
            ack.m_Losses = DataHeader->m_TxCount - DataHeader->m_ExpectedRank;
            sendto(c_Reception->c_Socket, (u08*)&ack, sizeof(ack), 0, (sockaddr*)sender_addr, sender_addr_len);
        }
        break;
    }
}


ReceptionSession::ReceptionSession(Reception * const Session, const sockaddr_in addr):c_Reception(Session), m_SenderAddress(addr)
{
    m_SequenceNumberForService = 0;
    m_MinSequenceNumberAwaitingAck = 0;
    m_MaxSequenceNumberAwaitingAck = 0;
}

ReceptionSession::~ReceptionSession()
{
    m_Blocks.DoSomethingOnAllData([](ReceptionBlock* &block){delete block;});
}

void ReceptionSession::Receive(u08* buffer, u16 length, const sockaddr_in * const sender_addr, const u32 sender_addr_len)
{
    Header::Data* const DataHeader = reinterpret_cast <Header::Data*>(buffer);
    // update min and max sequence.
    if(STRICTLY_ASCENDING_ORDER((m_MinSequenceNumberAwaitingAck-1), m_MinSequenceNumberAwaitingAck, ntohs(DataHeader->m_MinBlockSequenceNumber)))
    {
        for(; m_MinSequenceNumberAwaitingAck!=ntohs(DataHeader->m_MinBlockSequenceNumber) ; m_MinSequenceNumberAwaitingAck++)
        {
            if(m_MinSequenceNumberAwaitingAck==m_SequenceNumberForService)
            {
                // This is the case of best effort service.
                ReceptionBlock** const pp_block = m_Blocks.GetPtr(m_SequenceNumberForService);
                if(pp_block)
                {
                    ReceptionBlock* const p_block = (*pp_block);
                    if(p_block->Decoding() == false)
                    {
                        for(u08 i = 0 ; i < p_block->m_DecodedPacketBuffer.size() ; i++)
                        {
                            u08* pkt = p_block->m_DecodedPacketBuffer[i].release();
                            if(reinterpret_cast<Header::Data*>(pkt)->m_Flags & Header::Data::DataHeaderFlag::FLAGS_CONSUMED)
                            {
                                delete [] pkt;
                                continue;
                            }
                            while(m_RxTaskQueue.Enqueue([this, pkt](){
                                if(c_Reception->m_RxCallback)
                                {
                                    c_Reception->m_RxCallback(pkt+sizeof(Header::Data)+reinterpret_cast<Header::Data*>(pkt)->m_MaximumRank-1, ntohs(reinterpret_cast<Header::Data*>(pkt)->m_PayloadSize), &m_SenderAddress, sizeof(m_SenderAddress));
                                }
                                delete [] pkt;
                            })==false);
                        }
                    }
                }
                m_SequenceNumberForService++;
            }
            m_Blocks.Remove(m_MinSequenceNumberAwaitingAck, [this](ReceptionBlock* &data){
                while(m_RxTaskQueue.Enqueue([data](){
                    delete data;
                })==false);
            });
        }
    }
    if(STRICTLY_ASCENDING_ORDER((m_MaxSequenceNumberAwaitingAck-1), m_MaxSequenceNumberAwaitingAck, ntohs(DataHeader->m_MaxBlockSequenceNumber)))
    {
        m_MaxSequenceNumberAwaitingAck = ntohs(DataHeader->m_MaxBlockSequenceNumber);
    }
    if(STRICTLY_ASCENDING_ORDER(ntohs(DataHeader->m_CurrentBlockSequenceNumber), m_MinSequenceNumberAwaitingAck, m_MaxSequenceNumberAwaitingAck))
    {
        // If the sequence is less than min seq send ack and return.
        // But some packets can be received with significant delay.
        // Therefore, We must check if this packet is associated with the blocks in m_Blocks.
        ReceptionBlock** const pp_block = m_Blocks.GetPtr(ntohs(DataHeader->m_CurrentBlockSequenceNumber));
        if(pp_block && (*pp_block)->m_DecodingReady)
        {
            Header::DataAck ack;
            ack.m_Type = Header::Common::HeaderType::DATA_ACK;
            ack.m_Sequence = DataHeader->m_CurrentBlockSequenceNumber;
            ack.m_Losses = DataHeader->m_TxCount - DataHeader->m_ExpectedRank;
            sendto(c_Reception->c_Socket, (u08*)&ack, sizeof(ack), 0, (sockaddr*)sender_addr, sender_addr_len);
        }
        return;
    }

    ReceptionBlock** const pp_Block = m_Blocks.GetPtr(ntohs(DataHeader->m_CurrentBlockSequenceNumber));
    ReceptionBlock* p_Block = nullptr;
    if(pp_Block == nullptr)
    {
        try
        {
            TEST_EXCEPTION(std::bad_alloc());
            p_Block = new ReceptionBlock(c_Reception, this, ntohs(DataHeader->m_CurrentBlockSequenceNumber));
        }
        catch(const std::bad_alloc& ex)
        {
            EXCEPTION_PRINT;
            return;
        }
        if(m_Blocks.Insert(ntohs(DataHeader->m_CurrentBlockSequenceNumber), p_Block) == false)
        {
            delete p_Block;
            return;
        }
    }
    else
    {
        p_Block = (*pp_Block);
    }
    p_Block->Receive(buffer, length, sender_addr, sender_addr_len);
}

Reception::Reception(s32 Socket, std::function<void(u08* buffer, u16 length, const sockaddr_in * const sender_addr, const u32 sender_addr_len)> rx) : c_Socket(Socket), m_RxCallback(rx){}

Reception::~Reception()
{
    m_Sessions.DoSomethingOnAllData([](ReceptionSession* &session ){delete session;});
    m_Sessions.Clear();
}

void Reception::RxHandler(u08* buffer, u16 size, const sockaddr_in * const sender_addr, const u32 sender_addr_len)
{
    Header::Common* CommonHeader = reinterpret_cast< Header::Common* >(buffer);
    switch(CommonHeader->m_Type)
    {
        case Header::Common::HeaderType::DATA:
        {
            const DataStructures::IPv4PortKey key = {sender_addr->sin_addr.s_addr, sender_addr->sin_port};
            ReceptionSession** const pp_Session = m_Sessions.GetPtr(key);
            if(pp_Session == nullptr)
            {
                return;
            }
            ReceptionSession* const p_Session = (*pp_Session);
            p_Session->Receive(buffer, size, sender_addr, sender_addr_len);
        }
        break;

        case Header::Common::HeaderType::SYNC:
        {
            // create Rx Session.
            const DataStructures::IPv4PortKey key = {sender_addr->sin_addr.s_addr, sender_addr->sin_port};
            ReceptionSession** const pp_Session = m_Sessions.GetPtr(key);
            ReceptionSession* p_Session = nullptr;
            if(pp_Session == nullptr)
            {
                try
                {
                    TEST_EXCEPTION(std::bad_alloc());
                    p_Session = new ReceptionSession(this, (*sender_addr));
                }
                catch(const std::bad_alloc& ex)
                {
                    EXCEPTION_PRINT;
                    return;
                }
                if(m_Sessions.Insert(key, p_Session) == false)
                {
                    delete p_Session;
                    return;
                }
            }
            else
            {
                p_Session = (*pp_Session);
            }
            Header::Sync* const sync = reinterpret_cast< Header::Sync* >(buffer);
            p_Session->m_SequenceNumberForService = ntohs(sync->m_Sequence);
            p_Session->m_MinSequenceNumberAwaitingAck = ntohs(sync->m_Sequence);
            p_Session->m_MaxSequenceNumberAwaitingAck = ntohs(sync->m_Sequence);
            if(p_Session->m_Blocks.Size() > 0)
            {
                p_Session->m_Blocks.DoSomethingOnAllData([](ReceptionBlock* &block){delete block;});
                p_Session->m_Blocks.Clear();
            }
            sync->m_Type = Header::Common::HeaderType::SYNC_ACK;
            sendto(c_Socket, buffer, size, 0, (sockaddr*)sender_addr, sender_addr_len);
        }
        break;

        case Header::Data::HeaderType::PING:
        {
            Header::Ping* const ping = reinterpret_cast<Header::Ping*>(buffer);
            ping->m_Type = Header::Data::HeaderType::PONG;
            sendto(c_Socket, buffer, size, 0, (sockaddr*)sender_addr, sender_addr_len);
        }
        break;

        default:
        break;
    }

}
